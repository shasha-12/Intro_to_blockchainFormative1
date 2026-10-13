#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pending_pool.h"

#define INITIAL_CAPACITY 16

void pending_pool_init(PendingPool *pool) {
    pool->count = 0;
    pool->capacity = INITIAL_CAPACITY;
    pool->entries = malloc(sizeof(PendingEntry) * (size_t)pool->capacity);
    pool->next_request_id = 1;
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
        pool->entries = realloc(pool->entries, sizeof(PendingEntry) * (size_t)pool->capacity);
    }
}

void pending_pool_add(PendingPool *pool, const LendingRecord *entry) {
    ensure_capacity(pool);
    PendingEntry *e = &pool->entries[pool->count];
    e->request_id = pool->next_request_id++;
    e->record = *entry;
    e->status = POOL_STATUS_PENDING;
    e->priority = 2;
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
        if (strcmp(pool->entries[i].record.copy_id, copy_id) == 0) {
            return &pool->entries[i].record;
        }
    }
    return NULL;
}

/* The oldest (longest-waiting) reservation for book_id, without
 * consuming it - reservation_queue_pop_next is what actually consumes a
 * reservation, at return time. This is only used to rank priority. */
static int oldest_reservation_date(const ReservationQueue *reservations, const char *book_id,
                                    time_t *out_date) {
    for (int i = 0; i < reservations->count; i++) {
        if (strcmp(reservations->entries[i].book_id, book_id) == 0) {
            *out_date = reservations->entries[i].reservation_date;
            return 1;
        }
    }
    return 0;
}

static int assign_priority(const PendingEntry *e, const ReservationQueue *reservations) {
    if (strcmp(e->record.action, ACTION_OVERDUE) == 0) {
        return 0;
    }
    time_t unused;
    if (strcmp(e->record.action, ACTION_RETURNED) == 0 &&
        oldest_reservation_date(reservations, e->record.book_id, &unused)) {
        return 1;
    }
    return 2;
}

/* True if key belongs strictly before x: a lower priority number always
 * wins, and within priority 1 (a return someone is waiting on) the book
 * whose oldest reservation came first goes first. Equal priority returns
 * false - that's what keeps the insertion sort below stable, since ties
 * never get shifted past each other. */
static int belongs_before(const PendingEntry *key, const PendingEntry *x,
                           const ReservationQueue *reservations) {
    if (key->priority != x->priority) return key->priority < x->priority;
    if (key->priority == 1) {
        time_t key_date = 0, x_date = 0;
        oldest_reservation_date(reservations, key->record.book_id, &key_date);
        oldest_reservation_date(reservations, x->record.book_id, &x_date);
        return key_date < x_date;
    }
    return 0;
}

void pending_pool_prioritize(PendingPool *pool, const ReservationQueue *reservations) {
    for (int i = 0; i < pool->count; i++) {
        pool->entries[i].priority = assign_priority(&pool->entries[i], reservations);
    }

    for (int i = 1; i < pool->count; i++) {
        PendingEntry key = pool->entries[i];
        int j = i - 1;
        while (j >= 0 && belongs_before(&key, &pool->entries[j], reservations)) {
            pool->entries[j + 1] = pool->entries[j];
            j--;
        }
        pool->entries[j + 1] = key;
    }
}

static const char *format_timestamp(time_t ts, char *out, size_t out_size) {
    struct tm *tm_info = localtime(&ts);
    strftime(out, out_size, "%Y-%m-%d %H:%M:%S", tm_info);
    return out;
}

static const char *status_label(PoolEntryStatus status) {
    return status == POOL_STATUS_SUSPICIOUS ? "SUSPICIOUS" : "PENDING";
}

void pending_pool_print(const PendingPool *pool) {
    if (pool->count == 0) {
        printf("Pending pool is empty - nothing waiting to be mined.\n");
        return;
    }

    printf("--------------- PENDING POOL (%d unconfirmed) ---------------\n", pool->count);
    for (int i = 0; i < pool->count; i++) {
        const PendingEntry *e = &pool->entries[i];
        const LendingRecord *r = &e->record;
        char ts[32];
        format_timestamp(r->timestamp, ts, sizeof(ts));
        printf("[%d] req#%lu priority %d (%s) - %s - %s (%s / copy %s) by %s (%s) at %s\n",
               i, e->request_id, e->priority, status_label(e->status), r->action, r->book_title,
               r->book_id, r->copy_id, r->member_name, r->member_id, ts);
    }
    printf("---------------------------------------------------------------\n");
}
