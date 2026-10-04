#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pending_pool.h"

#define INITIAL_CAPACITY 16

void pending_pool_init(PendingPool *pool) {
    pool->count = 0;
    pool->capacity = INITIAL_CAPACITY;
    pool->entries = malloc(sizeof(Block) * (size_t)pool->capacity);
}

void pending_pool_free(PendingPool *pool) {
    free(pool->entries);
    pool->entries = NULL;
    pool->count = 0;
    pool->capacity = 0;
}

static void ensure_capacity(PendingPool *pool) {
    if (pool->count >= pool->capacity) {
        pool->capacity *= 2;
        pool->entries = realloc(pool->entries, sizeof(Block) * (size_t)pool->capacity);
    }
}

/* A block lands here already signed - borrow/return signs the block the
 * moment it's created, before this call - so pending_pool_add never touches
 * the cryptography, only storage. Confirmation (mining) is what later gives
 * the block its real chain index, previous_hash, and nonce. */
void pending_pool_add(PendingPool *pool, const Block *entry) {
    ensure_capacity(pool);
    pool->entries[pool->count] = *entry;
    pool->count++;
}

/* Mining always confirms the oldest entry first (index 0), so this is what
 * mining.c calls once a block has been mined and appended to the chain -
 * shifting everything after it down to close the gap. */
void pending_pool_remove_at(PendingPool *pool, int i) {
    if (i < 0 || i >= pool->count) return;
    for (int j = i; j < pool->count - 1; j++) {
        pool->entries[j] = pool->entries[j + 1];
    }
    pool->count--;
}

/* Searches newest-first so a book's most recent pending action (which is
 * what actually determines whether it's currently out on loan) is found
 * before any older, already-superseded entry for the same book. */
const Block *pending_pool_last_action_for_book(const PendingPool *pool, const char *book_id) {
    for (int i = pool->count - 1; i >= 0; i--) {
        if (strcmp(pool->entries[i].book_id, book_id) == 0) {
            return &pool->entries[i];
        }
    }
    return NULL;
}

static const char *format_timestamp(time_t ts, char *out, size_t out_size) {
    struct tm *tm_info = localtime(&ts);
    strftime(out, out_size, "%Y-%m-%d %H:%M:%S", tm_info);
    return out;
}

void pending_pool_print(const PendingPool *pool) {
    if (pool->count == 0) {
        printf("Pending pool is empty - nothing waiting to be mined.\n");
        return;
    }

    printf("--------------- PENDING POOL (%d unconfirmed) ---------------\n", pool->count);
    for (int i = 0; i < pool->count; i++) {
        const Block *b = &pool->entries[i];
        char ts[32];
        format_timestamp(b->timestamp, ts, sizeof(ts));
        printf("[%d] %s - %s (%s) by %s (%s) at %s\n",
               i, b->action, b->book_title, b->book_id,
               b->member_name, b->member_id, ts);
    }
    printf("---------------------------------------------------------------\n");
}
