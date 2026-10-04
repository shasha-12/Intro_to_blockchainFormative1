#ifndef UTXO_H
#define UTXO_H

#include "crypto.h"

#define UTXO_FEE 1

typedef struct {
    char txid[HASH_HEX_LEN];
    int output_index;
    char owner[20];
    int amount;
} UTXO;

typedef struct {
    UTXO *items;
    int count;
    int capacity;
} UTXOSet;

void utxo_set_init(UTXOSet *set);
void utxo_set_free(UTXOSet *set);

int utxo_balance(const UTXOSet *set, const char *owner);

/* Creates a single output with no fee deducted - used for crediting a
 * miner's block reward, which isn't a transaction "between" two parties
 * the way a reward or transfer is. */
int utxo_credit_direct(UTXOSet *set, const char *owner, int amount, const char *txid);

/* Creates `amount` new coins out of nothing (a reward/coinbase-style
 * transaction) minus the fixed fee, which goes to fee_owner. txid is
 * supplied by the caller (see crypto_compute_transaction_id) rather than
 * generated here, since the id often needs to be fixed and signed into a
 * block well before the mint actually happens at confirmation time.
 * Returns 1 on success, 0 if amount doesn't cover the fee. */
int utxo_mint(UTXOSet *set, const char *to_owner, int amount,
              const char *fee_owner, const char *txid);

/* Moves `amount` coins from from_owner to to_owner. Selects from_owner's
 * unspent outputs until their sum covers amount+fee, removing them from
 * the set (this is what prevents double-spending - a consumed UTXO no
 * longer exists to be spent again), credits to_owner and fee_owner, and
 * returns any excess to from_owner as a change output. Returns 1 on
 * success, 0 if from_owner's available balance can't cover amount+fee
 * (nothing is modified in that case). */
int utxo_transfer(UTXOSet *set, const char *from_owner, const char *to_owner,
                   int amount, const char *fee_owner, const char *txid);

void utxo_print_set(const UTXOSet *set);

#endif
