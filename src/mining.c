#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mining.h"

#define NETWORK_ACCOUNT "NETWORK"
#define REWARD_ON_TIME 10
#define REWARD_LATE 5

static int hash_meets_difficulty(const char *hash_hex, int difficulty) {
    for (int i = 0; i < difficulty; i++) {
        if (hash_hex[i] != '0') return 0;
    }
    return 1;
}

static unsigned long mine_block(Block *b, int difficulty) {
    unsigned long attempts = 0;
    b->nonce = 0;
    for (;;) {
        blockchain_recompute_hash(b, b->hash);
        attempts++;
        if (hash_meets_difficulty(b->hash, difficulty)) break;
        b->nonce++;
    }
    return attempts;
}

static int sets_due_date(const char *action) {
    return strcmp(action, ACTION_BORROWED) == 0 || strcmp(action, ACTION_RENEWED) == 0;
}

/* A RETURNED record doesn't carry its own reward amount - it's derived
 * deterministically from the most recent BORROWED-or-RENEWED record's
 * due_date versus this record's own timestamp, so the reward is always a
 * function of on-chain history rather than a separately trusted value.
 * RENEWED has to count here too, not just BORROWED - otherwise a renewed
 * loan would always be judged against its original, now-stale due date
 * instead of the extended one. The match may be sitting earlier in the
 * same batch (not yet on the chain) or already confirmed, so both are
 * checked. */
static int find_due_date_for_copy(const Blockchain *chain, const LendingRecord *batch,
                                   int batch_pos, const char *copy_id, time_t *out_due_date) {
    for (int i = batch_pos - 1; i >= 0; i--) {
        if (strcmp(batch[i].copy_id, copy_id) == 0 && sets_due_date(batch[i].action)) {
            *out_due_date = batch[i].due_date;
            return 1;
        }
    }
    const LendingRecord *last = blockchain_last_action_for_copy(chain, copy_id);
    if (last != NULL && sets_due_date(last->action)) {
        *out_due_date = last->due_date;
        return 1;
    }
    return 0;
}

static int determine_return_reward(const Blockchain *chain, const LendingRecord *batch,
                                    int batch_pos) {
    time_t due_date;
    if (!find_due_date_for_copy(chain, batch, batch_pos, batch[batch_pos].copy_id, &due_date)) {
        return 0;
    }
    return (batch[batch_pos].timestamp <= due_date) ? REWARD_ON_TIME : REWARD_LATE;
}

static void credit_member_reward(const char *member_id, int amount, const char *txid,
                                  TransactionModel model, UTXOSet *utxos, AccountLedger *accounts) {
    if (amount <= 0) return;
    if (model == MODEL_UTXO) {
        utxo_mint(utxos, member_id, amount, NETWORK_ACCOUNT, txid);
    } else {
        account_credit_reward(accounts, member_id, amount, NETWORK_ACCOUNT);
    }
}

static void credit_miner_amount(const char *miner_id, int amount, TransactionModel model,
                                 UTXOSet *utxos, AccountLedger *accounts) {
    if (amount <= 0) return;
    if (model == MODEL_UTXO) {
        char txid[HASH_HEX_LEN];
        crypto_compute_transaction_id("MINING_REWARD", miner_id, amount, time(NULL), txid);
        utxo_credit_direct(utxos, miner_id, amount, txid);
    } else {
        account_credit_direct(accounts, miner_id, amount);
    }
}

/* The brief requires the full UTXO set (or account balances) displayed
 * after each confirmed block, not just once at the end of a mining
 * command that may seal several blocks in one run. */
static void print_ledger_after_block(TransactionModel model, UTXOSet *utxos, AccountLedger *accounts) {
    if (model == MODEL_UTXO) {
        utxo_print_set(utxos);
    } else {
        account_print_all(accounts);
    }
}

/* Gathers up to MAX_RECORDS_PER_BLOCK candidate records from the front
 * of the pool (already priority-ordered by the caller). Anything flagged
 * SUSPICIOUS is skipped, and each remaining candidate must pass two
 * checks before being accepted: its signature re-verifies against its
 * claimed signer's current key, and its nonce matches the member's
 * CONFIRMED nonce (which only advances on confirmation, never when a
 * request merely enters the pool). Either failure leaves the record in
 * the pool rather than silently including it. Writes the indices (into
 * pool->entries) of every accepted record into out_indices and returns
 * how many were accepted. */
static int gather_valid_batch(const PendingPool *pool, NonceRegistry *nonces, MemberKeyStore *keys,
                               LendingRecord *out_batch, int *out_indices) {
    int accepted = 0;
    unsigned long projected[MAX_RECORDS_PER_BLOCK];
    char projected_member[MAX_RECORDS_PER_BLOCK][20];
    int projected_count = 0;

    for (int i = 0; i < pool->count && accepted < MAX_RECORDS_PER_BLOCK; i++) {
        if (pool->entries[i].status == POOL_STATUS_SUSPICIOUS) {
            continue;
        }
        const LendingRecord *candidate = &pool->entries[i].record;

        EVP_PKEY *signer_key = member_key_store_get(keys, candidate->member_id);
        if (signer_key == NULL || !blockchain_verify_record(candidate, signer_key)) {
            printf("  Rejected pending record for %s - invalid or missing signature (left in pool).\n",
                   candidate->member_id);
            continue;
        }

        unsigned long expected = nonce_registry_get(nonces, candidate->member_id);
        for (int j = 0; j < projected_count; j++) {
            if (strcmp(projected_member[j], candidate->member_id) == 0) {
                expected = projected[j];
            }
        }

        if (candidate->nonce != expected) {
            printf("  Rejected pending record for %s - nonce %lu does not match expected %lu (left in pool).\n",
                   candidate->member_id, candidate->nonce, expected);
            continue;
        }

        out_batch[accepted] = *candidate;
        out_indices[accepted] = i;
        accepted++;

        int found = 0;
        for (int j = 0; j < projected_count; j++) {
            if (strcmp(projected_member[j], candidate->member_id) == 0) {
                projected[j] = expected + 1;
                found = 1;
                break;
            }
        }
        if (!found) {
            strncpy(projected_member[projected_count], candidate->member_id, 19);
            projected_member[projected_count][19] = '\0';
            projected[projected_count] = expected + 1;
            projected_count++;
        }
    }

    return accepted;
}

/* Seals whatever gather_valid_batch accepts into one block with a real
 * merkle root, mines it, credits any RETURNED records' rewards and any
 * accrued fines, and advances each included record's sender's nonce.
 * This is the shared confirmation step behind all three mining modes -
 * they only differ in how the resulting block reward is distributed
 * afterward. Returns 0 (nothing confirmed, out_block untouched) if every
 * remaining pending record currently fails its nonce check. */
static int confirm_batch(Blockchain *chain, PendingPool *pool, int difficulty,
                          TransactionModel model, UTXOSet *utxos, AccountLedger *accounts,
                          FineLedger *fines, NonceRegistry *nonces, MemberKeyStore *keys,
                          Block *out_block, unsigned long *out_attempts) {
    LendingRecord batch[MAX_RECORDS_PER_BLOCK];
    int indices[MAX_RECORDS_PER_BLOCK];
    int batch_count = gather_valid_batch(pool, nonces, keys, batch, indices);
    if (batch_count == 0) {
        return 0;
    }

    /* The entries are genuinely CONFIRMED the instant they're accepted
     * into a sealed block - marked here mainly so the pending pool's own
     * status field (PENDING/CONFIRMED/SUSPICIOUS) is complete, even
     * though a confirmed entry is removed from the pool a few lines
     * below rather than lingering with that status. */
    for (int i = 0; i < batch_count; i++) {
        pool->entries[indices[i]].status = POOL_STATUS_CONFIRMED;
    }

    int total_reward = 0;
    for (int i = 0; i < batch_count; i++) {
        if (strcmp(batch[i].action, ACTION_RETURNED) == 0) {
            total_reward += determine_return_reward(chain, batch, i);
        }
    }

    Block b;
    blockchain_seal_block(&b, batch, batch_count, chain->count,
                           chain->blocks[chain->count - 1].hash,
                           chain->blocks[chain->count - 1].timestamp, "MINER", difficulty);
    b.token_reward = total_reward;
    if (total_reward > 0) {
        crypto_compute_transaction_id("SYSTEM", "BLOCK_REWARD", total_reward, b.timestamp, b.transaction_id);
    }

    *out_attempts = mine_block(&b, difficulty);
    blockchain_append_confirmed(chain, &b);

    for (int i = 0; i < batch_count; i++) {
        if (strcmp(batch[i].action, ACTION_RETURNED) == 0) {
            int reward = determine_return_reward(chain, batch, i);
            if (reward > 0) {
                credit_member_reward(batch[i].member_id, reward, b.transaction_id, model, utxos, accounts);
            }
        }
        /* A fine only becomes real once its record is confirmed - the
         * same "nothing counts until it's mined" rule as token rewards. */
        if (batch[i].fine_amount > 0) {
            fine_ledger_accrue(fines, batch[i].member_id, batch[i].fine_amount);
        }
        nonce_registry_increment(nonces, batch[i].member_id);
    }

    /* Remove accepted entries highest-index-first so earlier indices in
     * `indices` stay valid while removing - some lower-indexed pool
     * entries may have been skipped (left behind) rather than accepted. */
    for (int i = batch_count - 1; i >= 0; i--) {
        pending_pool_remove_at(pool, indices[i]);
    }

    *out_block = b;
    return 1;
}

int mining_solo(Blockchain *chain, PendingPool *pool, int difficulty, const char *miner_id,
                 TransactionModel model, UTXOSet *utxos, AccountLedger *accounts, FineLedger *fines,
                 NonceRegistry *nonces, const ReservationQueue *reservations, MemberKeyStore *keys) {
    if (pool->count == 0) {
        printf("Pending pool is empty - nothing to mine.\n");
        return 0;
    }

    pending_pool_prioritize(pool, reservations);

    printf("Mining %d pending record(s) at difficulty %d (miner: %s)...\n",
           pool->count, difficulty, miner_id);

    int blocks_confirmed = 0;
    unsigned long total_attempts = 0;

    while (pool->count > 0) {
        Block b;
        unsigned long attempts;
        if (!confirm_batch(chain, pool, difficulty, model, utxos, accounts, fines, nonces, keys, &b, &attempts)) {
            printf("  %d record(s) left in the pool could not be confirmed this round (nonce mismatch).\n",
                   pool->count);
            break;
        }
        total_attempts += attempts;
        blocks_confirmed++;

        printf("  Sealed block %d with %d record(s) - %lu hash attempt(s), hash %.12s...",
               b.index, b.record_count, attempts, b.hash);
        if (b.token_reward > 0) {
            printf(" (+%d coins in returns)", b.token_reward);
        }
        printf("\n");
        print_ledger_after_block(model, utxos, accounts);
    }

    int reward = MINING_BLOCK_REWARD * blocks_confirmed;
    credit_miner_amount(miner_id, reward, model, utxos, accounts);

    printf("Mining complete: %d block(s) confirmed, %lu total hash attempt(s).\n",
           blocks_confirmed, total_attempts);
    printf("Miner '%s' credited %d coins in block rewards.\n", miner_id, reward);

    return blocks_confirmed;
}

int mining_pool(Blockchain *chain, PendingPool *pool, int difficulty, int num_miners,
                 TransactionModel model, UTXOSet *utxos, AccountLedger *accounts, FineLedger *fines,
                 NonceRegistry *nonces, const ReservationQueue *reservations, MemberKeyStore *keys,
                 PoolMinerResult *out_results) {
    if (pool->count == 0) {
        printf("Pending pool is empty - nothing to mine.\n");
        return 0;
    }
    if (num_miners < 1) num_miners = 1;
    if (num_miners > MAX_POOL_MINERS) num_miners = MAX_POOL_MINERS;

    pending_pool_prioritize(pool, reservations);

    printf("Pool mining %d pending record(s) at difficulty %d with %d miner(s)...\n",
           pool->count, difficulty, num_miners);

    int blocks_confirmed = 0;
    unsigned long total_attempts = 0;

    while (pool->count > 0) {
        Block b;
        unsigned long attempts;
        if (!confirm_batch(chain, pool, difficulty, model, utxos, accounts, fines, nonces, keys, &b, &attempts)) {
            printf("  %d record(s) left in the pool could not be confirmed this round (nonce mismatch).\n",
                   pool->count);
            break;
        }
        total_attempts += attempts;
        blocks_confirmed++;
        printf("  Sealed block %d with %d record(s) - %lu hash attempt(s)\n",
               b.index, b.record_count, attempts);
        print_ledger_after_block(model, utxos, accounts);
    }

    int block_reward_total = MINING_BLOCK_REWARD * blocks_confirmed;
    int pool_fee = (block_reward_total * POOL_FEE_PERCENT) / 100;
    int distributable = block_reward_total - pool_fee;

    int weights[MAX_POOL_MINERS];
    int weight_sum = 0;
    for (int i = 0; i < num_miners; i++) {
        weights[i] = (rand() % 100) + 1;
        weight_sum += weights[i];
    }

    unsigned long assigned_sum = 0;
    int reward_sum = 0;
    for (int i = 0; i < num_miners; i++) {
        snprintf(out_results[i].miner_id, sizeof(out_results[i].miner_id), "POOL_MINER_%d", i + 1);

        unsigned long attempts;
        int reward;
        if (i == num_miners - 1) {
            attempts = total_attempts - assigned_sum;
            reward = distributable - reward_sum;
        } else {
            attempts = (unsigned long)((double)weights[i] / weight_sum * (double)total_attempts);
            reward = (int)((double)weights[i] / weight_sum * (double)distributable);
        }
        assigned_sum += attempts;
        reward_sum += reward;

        out_results[i].attempts = attempts;
        out_results[i].share_percent = total_attempts > 0
            ? (100.0 * (double)attempts / (double)total_attempts) : 0.0;
        out_results[i].reward = reward;

        credit_miner_amount(out_results[i].miner_id, reward, model, utxos, accounts);
    }

    printf("Pool mining complete: %d block(s) confirmed, %lu total hash attempt(s).\n",
           blocks_confirmed, total_attempts);
    printf("Block reward pool: %d coins, pool fee (%d%%): %d coins, distributed: %d coins\n",
           block_reward_total, POOL_FEE_PERCENT, pool_fee, distributable);
    printf("-------------------------- POOL MINING RESULTS --------------------------\n");
    printf("%-16s %-12s %-10s %s\n", "miner", "attempts", "share", "reward");
    for (int i = 0; i < num_miners; i++) {
        printf("%-16s %-12lu %-9.2f%% %d\n",
               out_results[i].miner_id, out_results[i].attempts,
               out_results[i].share_percent, out_results[i].reward);
    }
    printf("---------------------------------------------------------------------------\n");

    return blocks_confirmed;
}

void mining_cloud(Blockchain *chain, PendingPool *pool, int difficulty, int rounds,
                   const char *renter_id, TransactionModel model, UTXOSet *utxos, AccountLedger *accounts,
                   FineLedger *fines, NonceRegistry *nonces, const ReservationQueue *reservations,
                   MemberKeyStore *keys, CloudRound *out_rounds) {
    if (rounds < MIN_CLOUD_ROUNDS) rounds = MIN_CLOUD_ROUNDS;
    if (rounds > MAX_CLOUD_ROUNDS) rounds = MAX_CLOUD_ROUNDS;

    pending_pool_prioritize(pool, reservations);

    printf("Cloud mining for %d round(s), renting hash power for '%s'...\n", rounds, renter_id);

    int cumulative_fee = 0;
    int cumulative_reward = 0;

    for (int r = 0; r < rounds; r++) {
        while (pool->count > 0) {
            Block b;
            unsigned long attempts;
            if (!confirm_batch(chain, pool, difficulty, model, utxos, accounts, fines, nonces, keys, &b, &attempts)) {
                printf("  [round %d] %d record(s) left in the pool could not be confirmed (nonce mismatch).\n",
                       r + 1, pool->count);
                break;
            }
            printf("  [round %d] sealed block %d with %d record(s)\n", r + 1, b.index, b.record_count);
            print_ledger_after_block(model, utxos, accounts);
        }

        int fee = CLOUD_RENTAL_FEE;
        int reward = rand() % 31;

        cumulative_fee += fee;
        cumulative_reward += reward;

        CloudRound *cr = &out_rounds[r];
        cr->round = r + 1;
        cr->fee = fee;
        cr->reward = reward;
        cr->cumulative_fee = cumulative_fee;
        cr->cumulative_reward = cumulative_reward;
        cr->net_profit = cumulative_reward - cumulative_fee;
        cr->unprofitable = cumulative_fee > cumulative_reward;

        printf("  Round %d: fee %d, reward %d, cumulative fee %d, cumulative reward %d, net %d%s\n",
               cr->round, cr->fee, cr->reward, cr->cumulative_fee, cr->cumulative_reward,
               cr->net_profit, cr->unprofitable ? "  [WARNING: unprofitable so far]" : "");
    }

    int final_net = out_rounds[rounds - 1].net_profit;
    printf("Cloud mining complete: gross earnings %d, total fees paid %d, net profit %d.\n",
           cumulative_reward, cumulative_fee, final_net);

    if (final_net > 0) {
        credit_miner_amount(renter_id, final_net, model, utxos, accounts);
        printf("Net profit of %d coins credited to '%s'.\n", final_net, renter_id);
    } else {
        printf("No net profit to credit - the rental lost %d coins overall.\n", -final_net);
    }
}
