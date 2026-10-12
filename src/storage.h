#ifndef STORAGE_H
#define STORAGE_H

#include "blockchain.h"
#include "pending_pool.h"
#include "utxo.h"
#include "account.h"
#include "inventory.h"
#include "fines.h"

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

/* Persists the live copy inventory (including state/holder/due_date) so
 * a restart doesn't silently reset every copy back to AVAILABLE. Loaded
 * in preference to copies.txt - that file is only consulted on a genuine
 * first run, when no saved inventory exists yet. */
int storage_load_copy_inventory(CopyInventory *inv, const char *path);

int storage_save_copy_inventory(const CopyInventory *inv, const char *path);

int storage_load_reservation_queue(ReservationQueue *q, const char *path);

int storage_save_reservation_queue(const ReservationQueue *q, const char *path);

int storage_load_fine_ledger(FineLedger *ledger, const char *path);

int storage_save_fine_ledger(const FineLedger *ledger, const char *path);

#endif
