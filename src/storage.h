#ifndef STORAGE_H
#define STORAGE_H

#include "blockchain.h"
#include "pending_pool.h"
#include "utxo.h"
#include "account.h"

int storage_load_chain(Blockchain *chain, const char *path);

int storage_save_chain(const Blockchain *chain, const char *path);

int storage_load_pending_pool(PendingPool *pool, const char *path);

int storage_save_pending_pool(const PendingPool *pool, const char *path);

int storage_load_utxo_set(UTXOSet *set, const char *path);

int storage_save_utxo_set(const UTXOSet *set, const char *path);

/* Persists only balances and nonces, not the linked-list transaction
 * history - that's kept in memory only, per the assignment's own
 * description of it as an in-memory structure. History starts empty on
 * every fresh run even if a ledger file is loaded. */
int storage_load_account_ledger(AccountLedger *ledger, const char *path);

int storage_save_account_ledger(const AccountLedger *ledger, const char *path);

#endif
