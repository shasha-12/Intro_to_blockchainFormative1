#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inventory.h"

#define INITIAL_CAPACITY 32

void copy_inventory_init(CopyInventory *inv) {
    inv->count = 0;
    inv->capacity = INITIAL_CAPACITY;
    inv->copies = malloc(sizeof(Copy) * (size_t)inv->capacity);
}

void copy_inventory_free(CopyInventory *inv) {
    free(inv->copies);
    inv->copies = NULL;
    inv->count = 0;
    inv->capacity = 0;
}

static void ensure_capacity(CopyInventory *inv) {
    if (inv->count >= inv->capacity) {
        inv->capacity *= 2;
        inv->copies = realloc(inv->copies, sizeof(Copy) * (size_t)inv->capacity);
    }
}

static void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

int copy_inventory_load(CopyInventory *inv, const char *path, const Registry *reg) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: Could not open copy inventory file '%s'\n", path);
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        trim_newline(line);
        if (line[0] == '\0') continue;

        char copy_id[20] = {0}, book_id[20] = {0}, condition[10] = {0};
        char *token = strtok(line, ",");
        if (token) strncpy(copy_id, token, sizeof(copy_id) - 1);
        token = strtok(NULL, ",");
        if (token) strncpy(book_id, token, sizeof(book_id) - 1);
        token = strtok(NULL, ",");
        if (token) strncpy(condition, token, sizeof(condition) - 1);

        if (copy_id[0] == '\0' || book_id[0] == '\0' || condition[0] == '\0') {
            fprintf(stderr, "WARNING: Skipping malformed copy entry: %s\n", line);
            continue;
        }
        if (registry_find_book(reg, book_id) == NULL) {
            fprintf(stderr, "WARNING: Skipping copy '%s' - book_id '%s' not in registry\n", copy_id, book_id);
            continue;
        }

        ensure_capacity(inv);
        Copy *c = &inv->copies[inv->count];
        strncpy(c->copy_id, copy_id, sizeof(c->copy_id) - 1);
        strncpy(c->book_id, book_id, sizeof(c->book_id) - 1);
        strncpy(c->condition, condition, sizeof(c->condition) - 1);
        strncpy(c->state, COPY_STATE_AVAILABLE, sizeof(c->state) - 1);
        c->current_holder_id[0] = '\0';
        c->due_date = 0;
        inv->count++;
    }
    fclose(fp);

    if (inv->count == 0) {
        fprintf(stderr, "ERROR: Copy inventory file '%s' is empty\n", path);
        return 0;
    }
    return 1;
}

Copy *copy_inventory_find(CopyInventory *inv, const char *copy_id) {
    for (int i = 0; i < inv->count; i++) {
        if (strcmp(inv->copies[i].copy_id, copy_id) == 0) {
            return &inv->copies[i];
        }
    }
    return NULL;
}

Copy *copy_inventory_find_available(CopyInventory *inv, const char *book_id) {
    for (int i = 0; i < inv->count; i++) {
        if (strcmp(inv->copies[i].book_id, book_id) == 0 &&
            strcmp(inv->copies[i].state, COPY_STATE_AVAILABLE) == 0) {
            return &inv->copies[i];
        }
    }
    return NULL;
}

Copy *copy_inventory_find_reserved_for(CopyInventory *inv, const char *book_id, const char *member_id) {
    for (int i = 0; i < inv->count; i++) {
        if (strcmp(inv->copies[i].book_id, book_id) == 0 &&
            strcmp(inv->copies[i].state, COPY_STATE_RESERVED) == 0 &&
            strcmp(inv->copies[i].current_holder_id, member_id) == 0) {
            return &inv->copies[i];
        }
    }
    return NULL;
}

void copy_inventory_print_one(const Copy *c) {
    printf("  %-10s book=%-10s condition=%-6s state=%-10s holder=%-10s\n",
           c->copy_id, c->book_id, c->condition, c->state,
           c->current_holder_id[0] ? c->current_holder_id : "-");
}

void copy_inventory_print(const CopyInventory *inv) {
    if (inv->count == 0) {
        printf("No copies registered.\n");
        return;
    }
    printf("--------------------------- COPY INVENTORY ---------------------------\n");
    for (int i = 0; i < inv->count; i++) {
        copy_inventory_print_one(&inv->copies[i]);
    }
    printf("------------------------------------------------------------------------\n");
}

/* --- Reservation queue --- */

void reservation_queue_init(ReservationQueue *q) {
    q->count = 0;
    q->capacity = INITIAL_CAPACITY;
    q->entries = malloc(sizeof(Reservation) * (size_t)q->capacity);
}

void reservation_queue_free(ReservationQueue *q) {
    free(q->entries);
    q->entries = NULL;
    q->count = 0;
    q->capacity = 0;
}

static void reservation_ensure_capacity(ReservationQueue *q) {
    if (q->count >= q->capacity) {
        q->capacity *= 2;
        q->entries = realloc(q->entries, sizeof(Reservation) * (size_t)q->capacity);
    }
}

void reservation_queue_add(ReservationQueue *q, const char *book_id, const char *member_id) {
    reservation_ensure_capacity(q);
    Reservation *r = &q->entries[q->count];
    strncpy(r->book_id, book_id, sizeof(r->book_id) - 1);
    r->book_id[sizeof(r->book_id) - 1] = '\0';
    strncpy(r->member_id, member_id, sizeof(r->member_id) - 1);
    r->member_id[sizeof(r->member_id) - 1] = '\0';
    r->reservation_date = time(NULL);
    q->count++;
}

int reservation_queue_has_waiting(const ReservationQueue *q, const char *book_id) {
    for (int i = 0; i < q->count; i++) {
        if (strcmp(q->entries[i].book_id, book_id) == 0) return 1;
    }
    return 0;
}

int reservation_queue_pop_next(ReservationQueue *q, const char *book_id, char out_member_id[20]) {
    for (int i = 0; i < q->count; i++) {
        if (strcmp(q->entries[i].book_id, book_id) == 0) {
            strncpy(out_member_id, q->entries[i].member_id, 19);
            out_member_id[19] = '\0';
            for (int j = i; j < q->count - 1; j++) {
                q->entries[j] = q->entries[j + 1];
            }
            q->count--;
            return 1;
        }
    }
    return 0;
}

void reservation_queue_print(const ReservationQueue *q) {
    if (q->count == 0) {
        printf("No active reservations.\n");
        return;
    }
    printf("--------------------------- RESERVATION QUEUE ---------------------------\n");
    for (int i = 0; i < q->count; i++) {
        const Reservation *r = &q->entries[i];
        char ts[32];
        struct tm *tm_info = localtime(&r->reservation_date);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);
        printf("  book=%-10s member=%-10s since %s\n", r->book_id, r->member_id, ts);
    }
    printf("---------------------------------------------------------------------------\n");
}
