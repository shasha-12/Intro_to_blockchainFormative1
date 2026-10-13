#ifndef PENDING_POOL_H
#define PENDING_POOL_H

#include "blockchain.h"
#include "inventory.h"

typedef enum {
    POOL_STATUS_PENDING,
    POOL_STATUS_SUSPICIOUS,
} PoolEntryStatus;

/* Wraps a signed LendingRecord with the pool-level bookkeeping the
 * pending pool is responsible for on top of the record itself: a unique
 * request_id, a status (SUSPICIOUS once fraud review flags it), and a
 * priority rank that pending_pool_prioritize uses to reorder the array
 * itself ahead of mining. */
typedef struct {
    unsigned long request_id;
    LendingRecord record;
    PoolEntryStatus status;
    int priority;
} PendingEntry;

typedef struct {
    PendingEntry *entries;
    int count;
    int capacity;
    unsigned long next_request_id;
} PendingPool;

void pending_pool_init(PendingPool *pool);
void pending_pool_free(PendingPool *pool);

/* Wraps entry in a new PendingEntry (assigning the next request_id,
 * status PENDING) and appends it. */
void pending_pool_add(PendingPool *pool, const LendingRecord *entry);

/* Removes the entry at index i (shifts everything after it down by one).
 * Used by the miner once an entry has been batched into a sealed block. */
void pending_pool_remove_at(PendingPool *pool, int i);

/* Scans the pool (not the confirmed chain) for the most recent entry
 * referencing copy_id. Returns NULL if none is pending for that copy. */
const LendingRecord *pending_pool_last_action_for_copy(const PendingPool *pool, const char *copy_id);

/* Re-sorts the pool array in place by priority: OVERDUE records first,
 * then RETURNED requests for a book someone else is waiting on (earliest
 * reservation date first), then everything else in its original order.
 * Called before mining and before pool_view so both reflect the same
 * ordering. Stable - entries tied on priority keep their relative order,
 * which matters because mining still expects one member's own requests
 * to appear in the order their nonces were assigned. */
void pending_pool_prioritize(PendingPool *pool, const ReservationQueue *reservations);

void pending_pool_print(const PendingPool *pool);

#endif
