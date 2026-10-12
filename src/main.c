#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "registry.h"
#include "crypto.h"
#include "blockchain.h"
#include "pending_pool.h"
#include "mining.h"
#include "utxo.h"
#include "account.h"
#include "storage.h"

#define BOOKS_FILE "books.txt"
#define MEMBERS_FILE "members.txt"
#define CHAIN_FILE "chain.dat"
#define POOL_FILE "pending_pool.dat"
#define UTXO_FILE "utxo_set.dat"
#define ACCOUNT_FILE "account_ledger.dat"
#define NETWORK_ACCOUNT "NETWORK"
#define PRIV_KEY_FILE "keys/private.pem"
#define PUB_KEY_FILE "keys/public.pem"

/* A book returned within this many seconds of being borrowed counts as
 * on-time (10 coins); anything later counts as late (5 coins). A real
 * loan period would be measured in days, but that isn't demoable inside
 * a short video, so this is a deliberate simulation shortcut - documented
 * as such in the technical report. */
#define LOAN_PERIOD_SECONDS 15

static const char *model_name(TransactionModel model) {
    return model == MODEL_UTXO ? "UTXO" : "Account-based";
}

/* Returns 1 on a normal read, 0 on EOF/read error (buf is left as an
 * empty string either way, so existing callers that ignore the return
 * value keep working - they just see an empty input, same as before).
 * The main menu loop checks this specifically so that running out of
 * piped/redirected input exits cleanly instead of spinning forever on
 * atoi("") never matching a menu case. */
static int read_line(char *buf, size_t size) {
    if (fgets(buf, (int)size, stdin) == NULL) {
        buf[0] = '\0';
        return 0;
    }
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    return 1;
}

static void print_menu(int difficulty, TransactionModel model) {
    printf("\n===== Library Lending Tracker ===== [active model: %s]\n", model_name(model));
    printf("1. Borrow Book\n");
    printf("2. Return Book\n");
    printf("3. View Pending Pool\n");
    printf("4. Mine Pending Pool (Solo)\n");
    printf("5. Mine Pending Pool (Pool)\n");
    printf("6. Mine Pending Pool (Cloud)\n");
    printf("7. View Lending Records (confirmed)\n");
    printf("8. Validate Chain\n");
    printf("--- UTXO model ---\n");
    printf("9. View Token Balances & UTXO Set\n");
    printf("10. Send Tokens (UTXO Transfer)\n");
    printf("11. Credit Test Reward (UTXO Mint - dev/test hook)\n");
    printf("--- Account model ---\n");
    printf("12. View Account Balances\n");
    printf("13. Send Tokens (Account Transfer)\n");
    printf("14. View Transaction History (Account model)\n");
    printf("15. Credit Test Reward (Account model - dev/test hook)\n");
    printf("--- System ---\n");
    printf("16. Set Mining Difficulty (current: %d)\n", difficulty);
    printf("17. Tamper With a Past Block (Demo)\n");
    printf("18. Exit\n");
    printf("Choose an option: ");
}

/* A copy's current loan status is decided by whichever record is more
 * recent: an unconfirmed one still sitting in the pending pool, or the
 * last confirmed one on the chain. The pending pool is checked first
 * since anything in it is, by definition, newer than the chain tip. */
static const LendingRecord *current_loan_status(const PendingPool *pool, const Blockchain *chain,
                                                  const char *copy_id) {
    const LendingRecord *pending = pending_pool_last_action_for_copy(pool, copy_id);
    if (pending != NULL) return pending;
    return blockchain_last_action_for_copy(chain, copy_id);
}

/* record_id just needs to be unique across the chain and pool, not
 * sequential or gap-free, so the simplest honest source is a counter
 * seeded from how many records already exist in both places. */
static unsigned long next_record_id(const Blockchain *chain, const PendingPool *pool) {
    unsigned long count = (unsigned long)pool->count;
    for (int i = 0; i < chain->count; i++) {
        count += (unsigned long)chain->blocks[i].record_count;
    }
    return count + 1;
}

static void handle_borrow(PendingPool *pool, const Blockchain *chain,
                           const Registry *reg, EVP_PKEY *pkey) {
    char book_id[20], member_id[20];

    printf("Enter Book ID: ");
    read_line(book_id, sizeof(book_id));
    printf("Enter Member ID: ");
    read_line(member_id, sizeof(member_id));

    const Book *book = registry_find_book(reg, book_id);
    const Member *member = registry_find_member(reg, member_id);

    if (book == NULL || member == NULL) {
        printf("ERROR: Book or Member not found\n");
        return;
    }

    /* Single-copy-per-title for now - copy_id tracks book_id directly
     * until copy-level inventory (copies.txt) lands. */
    const LendingRecord *last = current_loan_status(pool, chain, book_id);
    if (last != NULL && strcmp(last->action, ACTION_BORROWED) == 0) {
        printf("ERROR: '%s' is already on loan and has not been returned\n", book->title);
        return;
    }

    LendingRecord rec = {0};
    rec.record_id = next_record_id(chain, pool);
    strncpy(rec.book_id, book->book_id, sizeof(rec.book_id) - 1);
    strncpy(rec.copy_id, book->book_id, sizeof(rec.copy_id) - 1);
    strncpy(rec.book_title, book->title, sizeof(rec.book_title) - 1);
    strncpy(rec.member_id, member->member_id, sizeof(rec.member_id) - 1);
    strncpy(rec.member_name, member->full_name, sizeof(rec.member_name) - 1);
    strncpy(rec.action, ACTION_BORROWED, sizeof(rec.action) - 1);
    rec.timestamp = time(NULL);
    rec.due_date = rec.timestamp + LOAN_PERIOD_SECONDS;
    rec.fine_amount = 0;
    rec.nonce = 0;

    if (!blockchain_sign_record(&rec, pkey)) {
        printf("ERROR: Failed to sign borrow request\n");
        return;
    }

    pending_pool_add(pool, &rec);
    storage_save_pending_pool(pool, POOL_FILE);
    printf("OK: '%s' borrow by %s added to the pending pool - awaiting mining.\n",
           book->title, member->full_name);
}

static void handle_return(PendingPool *pool, const Blockchain *chain, EVP_PKEY *pkey) {
    char book_id[20];
    printf("Enter Book ID: ");
    read_line(book_id, sizeof(book_id));

    const LendingRecord *last = current_loan_status(pool, chain, book_id);
    if (last == NULL || strcmp(last->action, ACTION_BORROWED) != 0) {
        printf("ERROR: No active loan found for book ID '%s'\n", book_id);
        return;
    }

    LendingRecord rec = {0};
    rec.record_id = next_record_id(chain, pool);
    strncpy(rec.book_id, last->book_id, sizeof(rec.book_id) - 1);
    strncpy(rec.copy_id, last->copy_id, sizeof(rec.copy_id) - 1);
    strncpy(rec.book_title, last->book_title, sizeof(rec.book_title) - 1);
    strncpy(rec.member_id, last->member_id, sizeof(rec.member_id) - 1);
    strncpy(rec.member_name, last->member_name, sizeof(rec.member_name) - 1);
    strncpy(rec.action, ACTION_RETURNED, sizeof(rec.action) - 1);
    rec.timestamp = time(NULL);
    rec.due_date = last->due_date;
    rec.fine_amount = 0;
    rec.nonce = 0;

    if (!blockchain_sign_record(&rec, pkey)) {
        printf("ERROR: Failed to sign return request\n");
        return;
    }

    pending_pool_add(pool, &rec);
    storage_save_pending_pool(pool, POOL_FILE);
    printf("OK: '%s' return by %s added to the pending pool - awaiting mining (%s, reward decided on confirmation).\n",
           last->book_title, last->member_name,
           rec.timestamp <= last->due_date ? "on-time" : "late");
}

static void save_ledgers(TransactionModel model, UTXOSet *utxos, AccountLedger *accounts) {
    if (model == MODEL_UTXO) {
        storage_save_utxo_set(utxos, UTXO_FILE);
    } else {
        storage_save_account_ledger(accounts, ACCOUNT_FILE);
    }
}

/* The brief requires the full UTXO set to be shown after each confirmed
 * block, so mining prints the active ledger's state automatically instead
 * of leaving it to a separate menu option the user might not pick. */
static void print_ledger_state(TransactionModel model, UTXOSet *utxos, AccountLedger *accounts) {
    if (model == MODEL_UTXO) {
        utxo_print_set(utxos);
    } else {
        account_print_all(accounts);
    }
}

static void handle_mine_solo(Blockchain *chain, PendingPool *pool, int difficulty,
                              TransactionModel model, UTXOSet *utxos, AccountLedger *accounts) {
    char miner_id[50];
    printf("Enter Miner ID: ");
    read_line(miner_id, sizeof(miner_id));
    if (miner_id[0] == '\0') {
        strcpy(miner_id, "MINER1");
    }

    mining_solo(chain, pool, difficulty, miner_id, model, utxos, accounts);

    storage_save_chain(chain, CHAIN_FILE);
    storage_save_pending_pool(pool, POOL_FILE);
    save_ledgers(model, utxos, accounts);
    print_ledger_state(model, utxos, accounts);
}

static void handle_mine_pool(Blockchain *chain, PendingPool *pool, int difficulty,
                              TransactionModel model, UTXOSet *utxos, AccountLedger *accounts) {
    char input[16];
    printf("Number of pool miners (%d-%d, default %d): ", 1, MAX_POOL_MINERS, DEFAULT_POOL_MINERS);
    read_line(input, sizeof(input));
    int num_miners = input[0] == '\0' ? DEFAULT_POOL_MINERS : atoi(input);

    PoolMinerResult results[MAX_POOL_MINERS];
    mining_pool(chain, pool, difficulty, num_miners, model, utxos, accounts, results);

    storage_save_chain(chain, CHAIN_FILE);
    storage_save_pending_pool(pool, POOL_FILE);
    save_ledgers(model, utxos, accounts);
    print_ledger_state(model, utxos, accounts);
}

static void handle_mine_cloud(Blockchain *chain, PendingPool *pool, int difficulty,
                               TransactionModel model, UTXOSet *utxos, AccountLedger *accounts) {
    char renter_id[50], input[16];
    printf("Enter Renter ID: ");
    read_line(renter_id, sizeof(renter_id));
    if (renter_id[0] == '\0') {
        strcpy(renter_id, "CLOUD_RENTER");
    }
    printf("Number of rounds (%d-%d): ", MIN_CLOUD_ROUNDS, MAX_CLOUD_ROUNDS);
    read_line(input, sizeof(input));
    int rounds = atoi(input);

    CloudRound rounds_out[MAX_CLOUD_ROUNDS];
    mining_cloud(chain, pool, difficulty, rounds, renter_id, model, utxos, accounts, rounds_out);

    storage_save_chain(chain, CHAIN_FILE);
    storage_save_pending_pool(pool, POOL_FILE);
    save_ledgers(model, utxos, accounts);
    print_ledger_state(model, utxos, accounts);
}

static int handle_set_difficulty(int current) {
    printf("Enter new difficulty (%d-%d): ", MIN_DIFFICULTY, MAX_DIFFICULTY);
    char input[16];
    read_line(input, sizeof(input));
    int value = atoi(input);

    if (value < MIN_DIFFICULTY || value > MAX_DIFFICULTY) {
        printf("ERROR: Difficulty must be between %d and %d. Keeping current value (%d).\n",
               MIN_DIFFICULTY, MAX_DIFFICULTY, current);
        return current;
    }

    printf("Difficulty set to %d.\n", value);
    return value;
}

static void handle_utxo_send(UTXOSet *utxos) {
    char from[20], to[20], input[16];
    printf("From (member ID): ");
    read_line(from, sizeof(from));
    printf("To (member ID): ");
    read_line(to, sizeof(to));
    printf("Amount: ");
    read_line(input, sizeof(input));
    int amount = atoi(input);

    if (amount <= 0) {
        printf("ERROR: Amount must be positive\n");
        return;
    }

    char txid[HASH_HEX_LEN];
    crypto_compute_transaction_id(from, to, amount, time(NULL), txid);
    if (!utxo_transfer(utxos, from, to, amount, NETWORK_ACCOUNT, txid)) {
        printf("ERROR: '%s' does not have enough unspent balance to send %d (+%d fee)\n",
               from, amount, UTXO_FEE);
        return;
    }

    storage_save_utxo_set(utxos, UTXO_FILE);
    printf("OK: %d coins sent from %s to %s (fee: %d to %s). txid %.16s...\n",
           amount, from, to, UTXO_FEE, NETWORK_ACCOUNT, txid);
}

/* A manual test utility for crediting an arbitrary amount outside the
 * normal borrow/return/mine flow - useful for setting up balances to
 * demo "Send Tokens" or edge cases without waiting on a real loan cycle. */
static void handle_utxo_credit_test(UTXOSet *utxos) {
    char member_id[20], input[16];
    printf("Credit which member ID: ");
    read_line(member_id, sizeof(member_id));
    printf("Reward amount (e.g. 10 on-time, 5 late): ");
    read_line(input, sizeof(input));
    int amount = atoi(input);

    char txid[HASH_HEX_LEN];
    crypto_compute_transaction_id("SYSTEM", member_id, amount, time(NULL), txid);
    if (!utxo_mint(utxos, member_id, amount, NETWORK_ACCOUNT, txid)) {
        printf("ERROR: Reward amount must be greater than the %d-coin fee\n", UTXO_FEE);
        return;
    }

    storage_save_utxo_set(utxos, UTXO_FILE);
    printf("OK: %d coins minted for %s (%d fee to %s). txid %.16s...\n",
           amount, member_id, UTXO_FEE, NETWORK_ACCOUNT, txid);
}

static void handle_account_send(AccountLedger *ledger) {
    char from[20], to[20], input[16];
    printf("From (member ID): ");
    read_line(from, sizeof(from));

    unsigned long expected = account_get_or_create(ledger, from)->nonce;
    printf("To (member ID): ");
    read_line(to, sizeof(to));
    printf("Amount: ");
    read_line(input, sizeof(input));
    int amount = atoi(input);
    printf("Nonce (expected: %lu): ", expected);
    read_line(input, sizeof(input));
    unsigned long nonce = (unsigned long)strtoul(input, NULL, 10);

    if (amount <= 0) {
        printf("ERROR: Amount must be positive\n");
        return;
    }

    AccountResult result = account_transfer(ledger, from, to, amount, nonce, NETWORK_ACCOUNT);
    switch (result) {
        case ACCOUNT_OK:
            storage_save_account_ledger(ledger, ACCOUNT_FILE);
            printf("OK: %d coins sent from %s to %s (fee: %d to %s, nonce %lu used).\n",
                   amount, from, to, ACCOUNT_FEE, NETWORK_ACCOUNT, nonce);
            break;
        case ACCOUNT_ERR_BAD_NONCE:
            printf("ERROR: Incorrect or reused nonce (expected %lu, got %lu)\n", expected, nonce);
            break;
        case ACCOUNT_ERR_INSUFFICIENT_BALANCE:
            printf("ERROR: '%s' does not have enough balance to send %d (+%d fee)\n",
                   from, amount, ACCOUNT_FEE);
            break;
    }
}

static void handle_account_history(AccountLedger *ledger) {
    char member_id[20];
    printf("Member ID: ");
    read_line(member_id, sizeof(member_id));
    account_print_history(ledger, member_id);
}

/* A manual test utility, same purpose as handle_utxo_credit_test but for
 * the account model. */
static void handle_account_credit_test(AccountLedger *ledger) {
    char member_id[20], input[16];
    printf("Credit which member ID: ");
    read_line(member_id, sizeof(member_id));
    printf("Reward amount (e.g. 10 on-time, 5 late): ");
    read_line(input, sizeof(input));
    int amount = atoi(input);

    if (!account_credit_reward(ledger, member_id, amount, NETWORK_ACCOUNT)) {
        printf("ERROR: Reward amount must be greater than the %d-coin fee\n", ACCOUNT_FEE);
        return;
    }

    storage_save_account_ledger(ledger, ACCOUNT_FILE);
    printf("OK: %d coins credited to %s (%d fee to %s).\n",
           amount, member_id, ACCOUNT_FEE, NETWORK_ACCOUNT);
}

static void handle_tamper_demo(Blockchain *chain) {
    int tamperable = 0;
    for (int i = 1; i < chain->count; i++) {
        if (chain->blocks[i].record_count > 0) tamperable = 1;
    }
    if (!tamperable) {
        printf("No lending records yet to tamper with (only the genesis block exists).\n");
        return;
    }

    printf("Enter the index of the block to tamper with (1-%d): ", chain->count - 1);
    char input[16];
    read_line(input, sizeof(input));
    int idx = atoi(input);

    if (idx < 1 || idx >= chain->count || chain->blocks[idx].record_count == 0) {
        printf("ERROR: Invalid block index\n");
        return;
    }

    Block *b = &chain->blocks[idx];
    LendingRecord *rec = &b->records[0];
    printf("Tampering with block %d, record 0: book_title '%s' -> 'TAMPERED RECORD' (without re-signing, hash/merkle root left stale)\n",
           idx, rec->book_title);
    strncpy(rec->book_title, "TAMPERED RECORD", sizeof(rec->book_title) - 1);
    rec->book_title[sizeof(rec->book_title) - 1] = '\0';

    storage_save_chain(chain, CHAIN_FILE);
    printf("Block %d modified in memory and on disk. Run 'Validate Chain' to see it detected.\n", idx);
}

int main(int argc, char *argv[]) {
    int difficulty = DEFAULT_DIFFICULTY;
    TransactionModel model = MODEL_UTXO;
    int model_set_by_arg = 0;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--difficulty") == 0) {
            int value = atoi(argv[i + 1]);
            if (value >= MIN_DIFFICULTY && value <= MAX_DIFFICULTY) {
                difficulty = value;
            } else {
                fprintf(stderr, "WARNING: --difficulty must be %d-%d, ignoring '%s'\n",
                        MIN_DIFFICULTY, MAX_DIFFICULTY, argv[i + 1]);
            }
        } else if (strcmp(argv[i], "--model") == 0) {
            if (strcmp(argv[i + 1], "account") == 0) {
                model = MODEL_ACCOUNT;
                model_set_by_arg = 1;
            } else if (strcmp(argv[i + 1], "utxo") == 0) {
                model = MODEL_UTXO;
                model_set_by_arg = 1;
            } else {
                fprintf(stderr, "WARNING: --model must be 'utxo' or 'account', ignoring '%s'\n",
                        argv[i + 1]);
            }
        }
    }

    srand((unsigned int)time(NULL));

    Registry reg;
    if (!registry_load(&reg, BOOKS_FILE, MEMBERS_FILE)) {
        fprintf(stderr, "FATAL: Could not start — book/member registries missing or empty.\n");
        return 1;
    }
    printf("Loaded %d books and %d members.\n", reg.book_count, reg.member_count);

    EVP_PKEY *pkey = NULL;
    if (!crypto_load_or_generate_keys(PRIV_KEY_FILE, PUB_KEY_FILE, &pkey)) {
        fprintf(stderr, "FATAL: Could not load or generate signing keys.\n");
        return 1;
    }

    Blockchain chain;
    blockchain_init(&chain);
    if (!storage_load_chain(&chain, CHAIN_FILE)) {
        printf("No existing chain found — creating genesis block.\n");
        blockchain_add_genesis(&chain);
        storage_save_chain(&chain, CHAIN_FILE);
    } else {
        printf("Loaded existing chain with %d block(s).\n", chain.count);
    }

    PendingPool pool;
    pending_pool_init(&pool);
    if (storage_load_pending_pool(&pool, POOL_FILE) && pool.count > 0) {
        printf("Loaded %d pending record(s) from a previous session.\n", pool.count);
    }

    UTXOSet utxos;
    utxo_set_init(&utxos);
    storage_load_utxo_set(&utxos, UTXO_FILE);

    AccountLedger accounts;
    account_ledger_init(&accounts);
    storage_load_account_ledger(&accounts, ACCOUNT_FILE);

    if (!model_set_by_arg) {
        printf("\nChoose a transaction model for this session:\n");
        printf("  1. UTXO\n");
        printf("  2. Account-based\n");
        printf("Choice (default UTXO): ");
        char model_input[16];
        read_line(model_input, sizeof(model_input));
        model = (atoi(model_input) == 2) ? MODEL_ACCOUNT : MODEL_UTXO;
    }
    printf("Active transaction model: %s (applies to all token activity this session)\n",
           model_name(model));

    int running = 1;
    while (running) {
        print_menu(difficulty, model);
        char input[16];
        if (!read_line(input, sizeof(input))) {
            printf("\nInput closed - exiting.\n");
            break;
        }
        int choice = atoi(input);

        switch (choice) {
            case 1:
                handle_borrow(&pool, &chain, &reg, pkey);
                break;
            case 2:
                handle_return(&pool, &chain, pkey);
                break;
            case 3:
                pending_pool_print(&pool);
                break;
            case 4:
                handle_mine_solo(&chain, &pool, difficulty, model, &utxos, &accounts);
                break;
            case 5:
                handle_mine_pool(&chain, &pool, difficulty, model, &utxos, &accounts);
                break;
            case 6:
                handle_mine_cloud(&chain, &pool, difficulty, model, &utxos, &accounts);
                break;
            case 7:
                blockchain_print_records(&chain, pkey);
                break;
            case 8: {
                int valid = blockchain_validate(&chain, pkey);
                printf(valid ? "\nChain is VALID — no tampering detected.\n"
                             : "\nChain is INVALID — tampering detected!\n");
                break;
            }
            case 9:
                utxo_print_set(&utxos);
                break;
            case 10:
                if (model != MODEL_UTXO) {
                    printf("ERROR: UTXO model is not active this session (active: %s)\n", model_name(model));
                } else {
                    handle_utxo_send(&utxos);
                }
                break;
            case 11:
                if (model != MODEL_UTXO) {
                    printf("ERROR: UTXO model is not active this session (active: %s)\n", model_name(model));
                } else {
                    handle_utxo_credit_test(&utxos);
                }
                break;
            case 12:
                account_print_all(&accounts);
                break;
            case 13:
                if (model != MODEL_ACCOUNT) {
                    printf("ERROR: Account model is not active this session (active: %s)\n", model_name(model));
                } else {
                    handle_account_send(&accounts);
                }
                break;
            case 14:
                handle_account_history(&accounts);
                break;
            case 15:
                if (model != MODEL_ACCOUNT) {
                    printf("ERROR: Account model is not active this session (active: %s)\n", model_name(model));
                } else {
                    handle_account_credit_test(&accounts);
                }
                break;
            case 16:
                difficulty = handle_set_difficulty(difficulty);
                break;
            case 17:
                handle_tamper_demo(&chain);
                break;
            case 18:
                running = 0;
                break;
            default:
                printf("Invalid option. Please choose 1-18.\n");
        }
    }

    account_ledger_free(&accounts);
    utxo_set_free(&utxos);
    pending_pool_free(&pool);
    blockchain_free(&chain);
    crypto_free_key(pkey);
    return 0;
}
