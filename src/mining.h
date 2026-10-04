#ifndef MINING_H
#define MINING_H

#include "blockchain.h"
#include "pending_pool.h"
#include "utxo.h"
#include "account.h"

#define MINING_BLOCK_REWARD 50
#define MIN_DIFFICULTY 1
#define MAX_DIFFICULTY 4
#define DEFAULT_DIFFICULTY 2

#define POOL_FEE_PERCENT 2
#define MAX_POOL_MINERS 8
#define DEFAULT_POOL_MINERS 4

#define CLOUD_RENTAL_FEE 5
#define MIN_CLOUD_ROUNDS 1
#define MAX_CLOUD_ROUNDS 5

typedef enum {
    MODEL_UTXO,
    MODEL_ACCOUNT,
} TransactionModel;

typedef struct {
    char miner_id[20];
    unsigned long attempts;
    double share_percent;
    int reward;
} PoolMinerResult;

/* Mines every entry currently in the pool, oldest first: assigns each one
 * its real chain position, searches for a nonce whose resulting hash has
 * `difficulty` leading zero hex characters, appends it to the chain, and
 * (for a RETURNED block carrying a token_reward) credits the member
 * through whichever ledger model is active. The full per-block mining
 * reward goes to miner_id. Returns the number of blocks confirmed. */
int mining_solo(Blockchain *chain, PendingPool *pool, int difficulty, const char *miner_id,
                 TransactionModel model, UTXOSet *utxos, AccountLedger *accounts);

/* Same underlying confirmation process as mining_solo, but the combined
 * mining reward for everything confirmed in this run (minus a 2% pool
 * fee) is split across num_miners simulated miners, weighted by a
 * randomly assigned share of the real total hash attempts. Writes up to
 * num_miners results into out_results (caller-provided, at least
 * num_miners long) and returns how many blocks were confirmed. */
int mining_pool(Blockchain *chain, PendingPool *pool, int difficulty, int num_miners,
                 TransactionModel model, UTXOSet *utxos, AccountLedger *accounts,
                 PoolMinerResult *out_results);

typedef struct {
    int round;
    int fee;
    int reward;
    int cumulative_fee;
    int cumulative_reward;
    int net_profit;
    int unprofitable;
} CloudRound;

/* Confirms whatever is pending once per round (same as solo mining,
 * crediting member rewards normally), while separately tracking a
 * rental fee/reward simulation across `rounds` rounds (1-5). Writes one
 * CloudRound per round into out_rounds (caller-provided, at least
 * `rounds` long). Any positive net profit at the end is credited to
 * renter_id through the active ledger model. */
void mining_cloud(Blockchain *chain, PendingPool *pool, int difficulty, int rounds,
                   const char *renter_id, TransactionModel model,
                   UTXOSet *utxos, AccountLedger *accounts, CloudRound *out_rounds);

#endif
