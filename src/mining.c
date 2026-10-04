#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mining.h"

#define NETWORK_ACCOUNT "NETWORK"

static int hash_meets_difficulty(const char *hash_hex, int difficulty) {
    for (int i = 0; i < difficulty; i++) {
        if (hash_hex[i] != '0') return 0;
    }
    return 1;
}

/* Proof-of-work: repeatedly bump the nonce and rehash until the block's
 * hash starts with `difficulty` zero hex characters. Returns how many
 * attempts it took (always at least 1). */
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

/* Pops the oldest pending entry, assigns it a real chain position, mines
 * it, and appends it to the chain. Shared by all three mining modes -
 * they only differ in how the resulting mining reward is distributed. */
static Block confirm_one_pending(Blockchain *chain, PendingPool *pool,
                                  int difficulty, unsigned long *out_attempts) {
    Block b = pool->entries[0];
    b.index = chain->count;
    strncpy(b.previous_hash, chain->blocks[chain->count - 1].hash, sizeof(b.previous_hash) - 1);
    b.previous_hash[sizeof(b.previous_hash) - 1] = '\0';

    *out_attempts = mine_block(&b, difficulty);

    blockchain_append_confirmed(chain, &b);
    pending_pool_remove_at(pool, 0);
    return b;
}

/* A confirmed RETURNED block may carry a token_reward (10 on-time, 5
 * late, 0/none otherwise) determined back when the block was created.
 * This is what actually pays it out, through whichever ledger model is
 * active for the session. */
static void credit_member_reward(const Block *b, TransactionModel model,
                                  UTXOSet *utxos, AccountLedger *accounts) {
    if (b->token_reward <= 0) return;
    if (model == MODEL_UTXO) {
        utxo_mint(utxos, b->member_id, b->token_reward, NETWORK_ACCOUNT, b->transaction_id);
    } else {
        account_credit_reward(accounts, b->member_id, b->token_reward, NETWORK_ACCOUNT);
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

int mining_solo(Blockchain *chain, PendingPool *pool, int difficulty, const char *miner_id,
                 TransactionModel model, UTXOSet *utxos, AccountLedger *accounts) {
    if (pool->count == 0) {
        printf("Pending pool is empty - nothing to mine.\n");
        return 0;
    }

    printf("Mining %d pending block(s) at difficulty %d (miner: %s)...\n",
           pool->count, difficulty, miner_id);

    int confirmed = 0;
    unsigned long total_attempts = 0;

    while (pool->count > 0) {
        unsigned long attempts;
        Block b = confirm_one_pending(chain, pool, difficulty, &attempts);
        total_attempts += attempts;
        confirmed++;

        credit_member_reward(&b, model, utxos, accounts);

        printf("  Confirmed block %d [%s] %s (%s) - %lu hash attempt(s), hash %.12s...",
               b.index, b.action, b.book_title, b.book_id, attempts, b.hash);
        if (b.token_reward > 0) {
            printf(" (+%d coins to %s)", b.token_reward, b.member_id);
        }
        printf("\n");
    }

    int reward = MINING_BLOCK_REWARD * confirmed;
    credit_miner_amount(miner_id, reward, model, utxos, accounts);

    printf("Mining complete: %d block(s) confirmed, %lu total hash attempt(s).\n",
           confirmed, total_attempts);
    printf("Miner '%s' credited %d coins in block rewards.\n", miner_id, reward);

    return confirmed;
}

int mining_pool(Blockchain *chain, PendingPool *pool, int difficulty, int num_miners,
                 TransactionModel model, UTXOSet *utxos, AccountLedger *accounts,
                 PoolMinerResult *out_results) {
    if (pool->count == 0) {
        printf("Pending pool is empty - nothing to mine.\n");
        return 0;
    }
    if (num_miners < 1) num_miners = 1;
    if (num_miners > MAX_POOL_MINERS) num_miners = MAX_POOL_MINERS;

    printf("Pool mining %d pending block(s) at difficulty %d with %d miner(s)...\n",
           pool->count, difficulty, num_miners);

    int confirmed = 0;
    unsigned long total_attempts = 0;

    while (pool->count > 0) {
        unsigned long attempts;
        Block b = confirm_one_pending(chain, pool, difficulty, &attempts);
        total_attempts += attempts;
        confirmed++;

        credit_member_reward(&b, model, utxos, accounts);

        printf("  Confirmed block %d [%s] %s (%s) - %lu hash attempt(s)\n",
               b.index, b.action, b.book_title, b.book_id, attempts);
    }

    int block_reward_total = MINING_BLOCK_REWARD * confirmed;
    int pool_fee = (block_reward_total * POOL_FEE_PERCENT) / 100;
    int distributable = block_reward_total - pool_fee;

    /* Assign each simulated miner a random weight, then split the real
     * total_attempts proportionally - the last miner absorbs whatever
     * rounding remainder is left so the shares always sum exactly to
     * total_attempts. */
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
           confirmed, total_attempts);
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

    return confirmed;
}

void mining_cloud(Blockchain *chain, PendingPool *pool, int difficulty, int rounds,
                   const char *renter_id, TransactionModel model,
                   UTXOSet *utxos, AccountLedger *accounts, CloudRound *out_rounds) {
    if (rounds < MIN_CLOUD_ROUNDS) rounds = MIN_CLOUD_ROUNDS;
    if (rounds > MAX_CLOUD_ROUNDS) rounds = MAX_CLOUD_ROUNDS;

    printf("Cloud mining for %d round(s), renting hash power for '%s'...\n", rounds, renter_id);

    int cumulative_fee = 0;
    int cumulative_reward = 0;

    for (int r = 0; r < rounds; r++) {
        /* Whatever's pending gets confirmed each round, same as solo
         * mining - renting hash power still gets your blocks mined, you
         * just aren't the one doing the PoW search yourself. */
        while (pool->count > 0) {
            unsigned long attempts;
            Block b = confirm_one_pending(chain, pool, difficulty, &attempts);
            credit_member_reward(&b, model, utxos, accounts);
            printf("  [round %d] confirmed block %d [%s] %s (%s)\n",
                   r + 1, b.index, b.action, b.book_title, b.book_id);
        }

        int fee = CLOUD_RENTAL_FEE;
        int reward = rand() % 31; /* 0-30, so some rounds land below the fee on purpose */

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
