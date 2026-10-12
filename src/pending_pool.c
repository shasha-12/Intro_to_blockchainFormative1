#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pending_pool.h"

#define INITIAL_CAPACITY 16

void pending_pool_init(PendingPool *pool) {
    pool->count = 0;
    pool->capacity = INITIAL_CAPACITY;
    pool->entries = malloc(sizeof(LendingRecord) * (size_t)pool->capacity);
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
        pool->entries = realloc(pool->entries, sizeof(LendingRecord) * (size_t)pool->capacity);
    }
}

void pending_pool_add(PendingPool *pool, const LendingRecord *entry) {
    ensure_capacity(pool);
    pool->entries[pool->count] = *entry;
    pool->count++;
}

void pending_pool_remove_at(PendingPool *pool, int i) {
    if (i < 0 || i >= pool->count) return;
    for (int j = i; j < pool->count - 1; j++) {
        pool->entries[j] = pool->entries[j + 1];
    }
    pool->count--;
}

const LendingRecord *pending_pool_last_action_for_copy(const PendingPool *pool, const char *copy_id) {
    for (int i = pool->count - 1; i >= 0; i--) {
        if (strcmp(pool->entries[i].copy_id, copy_id) == 0) {
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
        const LendingRecord *r = &pool->entries[i];
        char ts[32];
        format_timestamp(r->timestamp, ts, sizeof(ts));
        printf("[%d] %s - %s (%s / copy %s) by %s (%s) at %s\n",
               i, r->action, r->book_title, r->book_id, r->copy_id,
               r->member_name, r->member_id, ts);
    }
    printf("---------------------------------------------------------------\n");
}
