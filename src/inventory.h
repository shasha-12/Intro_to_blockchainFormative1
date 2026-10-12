#ifndef INVENTORY_H
#define INVENTORY_H

#include <time.h>
#include "registry.h"

#define COPY_STATE_AVAILABLE "AVAILABLE"
#define COPY_STATE_BORROWED  "BORROWED"
#define COPY_STATE_RESERVED  "RESERVED"
#define COPY_STATE_LOST      "LOST"

typedef struct {
    char copy_id[20];
    char book_id[20];
    char condition[10];
    char state[12];
    char current_holder_id[20];
    time_t due_date;
} Copy;

typedef struct {
    Copy *copies;
    int count;
    int capacity;
} CopyInventory;

void copy_inventory_init(CopyInventory *inv);
void copy_inventory_free(CopyInventory *inv);

/* Loads copy_id,book_id,condition rows from copies.txt, validating each
 * book_id against the already-loaded book registry. Every copy starts
 * AVAILABLE with no holder - borrow/return/mark_lost are what move it
 * through the rest of the state machine from there. */
int copy_inventory_load(CopyInventory *inv, const char *path, const Registry *reg);

Copy *copy_inventory_find(CopyInventory *inv, const char *copy_id);

/* First AVAILABLE copy belonging to book_id, or NULL if every copy of
 * that title is out, reserved, or lost. */
Copy *copy_inventory_find_available(CopyInventory *inv, const char *book_id);

/* A copy of book_id sitting in RESERVED state held specifically for
 * member_id - checked before find_available, so a member picking up
 * their own reservation claims that exact copy rather than a different
 * generally-available one. */
Copy *copy_inventory_find_reserved_for(CopyInventory *inv, const char *book_id, const char *member_id);

void copy_inventory_print(const CopyInventory *inv);
void copy_inventory_print_one(const Copy *c);

typedef struct {
    char book_id[20];
    char member_id[20];
    time_t reservation_date;
} Reservation;

typedef struct {
    Reservation *entries;
    int count;
    int capacity;
} ReservationQueue;

void reservation_queue_init(ReservationQueue *q);
void reservation_queue_free(ReservationQueue *q);

void reservation_queue_add(ReservationQueue *q, const char *book_id, const char *member_id);

/* True if book_id has at least one member waiting. */
int reservation_queue_has_waiting(const ReservationQueue *q, const char *book_id);

/* Removes and returns (via out_member_id) the longest-waiting reservation
 * for book_id - FIFO, since entries are always appended at the back and
 * this always takes from the first matching entry found scanning forward
 * from index 0. Returns 0 if nobody is waiting for that title. */
int reservation_queue_pop_next(ReservationQueue *q, const char *book_id, char out_member_id[20]);

void reservation_queue_print(const ReservationQueue *q);

#endif
