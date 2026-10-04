#ifndef PENDING_POOL_H
#define PENDING_POOL_H

#include "blockchain.h"

typedef struct {
    Block *entries;
    int count;
    int capacity;
} PendingPool;

void pending_pool_init(PendingPool *pool);
void pending_pool_free(PendingPool *pool);

void pending_pool_add(PendingPool *pool, const Block *entry);

/* Removes the entry at index i (shifts everything after it down by one).
 * Used by the miner once an entry has been confirmed onto the chain. */
void pending_pool_remove_at(PendingPool *pool, int i);

/* Scans the pool (not the confirmed chain) for the most recent entry
 * referencing book_id. Returns NULL if none is pending for that book. */
const Block *pending_pool_last_action_for_book(const PendingPool *pool, const char *book_id);

void pending_pool_print(const PendingPool *pool);

#endif
