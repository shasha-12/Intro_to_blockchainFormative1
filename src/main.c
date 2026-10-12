#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "registry.h"
#include "crypto.h"
#include "blockchain.h"
#include "member_keys.h"
#include "nonce_registry.h"
#include "inventory.h"
#include "fines.h"
#include "pending_pool.h"
#include "mining.h"
#include "utxo.h"
#include "account.h"
#include "storage.h"

#define BOOKS_FILE "books.txt"
#define MEMBERS_FILE "members.txt"
#define COPIES_FILE "copies.txt"
#define CHAIN_FILE "chain.dat"
#define POOL_FILE "pending_pool.dat"
#define UTXO_FILE "utxo_set.dat"
#define ACCOUNT_FILE "account_ledger.dat"
#define COPY_STATE_FILE "copy_inventory.dat"
#define RESERVATION_FILE "reservation_queue.dat"
#define FINE_FILE "fine_ledger.dat"
#define NONCE_FILE "nonce_registry.dat"
#define NETWORK_ACCOUNT "NETWORK"

/* A book borrowed now is due this many seconds later, and each renewal
 * extends it by the same window - a real loan period would be measured
 * in days, but that isn't demoable inside a short video, so this is a
 * deliberate simulation shortcut, documented as such in the technical
 * report. SIMULATED_DAY_SECONDS is the same idea applied to "a day
 * overdue" for fine accrual, so a fine can actually be seen growing on
 * camera instead of requiring real days to pass. */
#define LOAN_PERIOD_SECONDS 15
#define RENEWAL_PERIOD_SECONDS 15
#define MAX_RENEWALS 2
#define SIMULATED_DAY_SECONDS 5
#define FINE_PER_DAY 2

static const char *model_name(TransactionModel model) {
    return model == MODEL_UTXO ? "UTXO" : "Account-based";
}

/* Returns 1 on a normal read, 0 on EOF/read error. The main command loop
 * checks this specifically so that running out of piped/redirected
 * input exits cleanly instead of spinning forever. */
static int read_line(char *buf, size_t size) {
    if (fgets(buf, (int)size, stdin) == NULL) {
        buf[0] = '\0';
        return 0;
    }
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    return 1;
}

/* Splits one input line into a command word and whatever (optionally
 * empty) trailing argument follows it, e.g. "mine_cloud 3" -> cmd=
 * "mine_cloud", arg="3". Commands that take their argument inline
 * (mine_cloud, transaction_history, mark_lost) use arg directly if
 * present and prompt for it otherwise. */
static void split_command(char *line, char **cmd, char **arg) {
    *cmd = line;
    *arg = NULL;
    char *space = strchr(line, ' ');
    if (space != NULL) {
        *space = '\0';
        char *rest = space + 1;
        while (*rest == ' ') rest++;
        if (*rest != '\0') *arg = rest;
    }
}

static void print_commands(int difficulty, TransactionModel model) {
    printf("\n===== Library Lending Tracker ===== [model: %s, difficulty: %d]\n",
           model_name(model), difficulty);
    printf("Membership:   register_member  view_member  member_nonce\n");
    printf("Book/Copy:    register_book  view_book  add_copy  view_copy  mark_lost\n");
    printf("Lending:      borrow_book  return_book  renew_book  reserve_book  reservation_status\n");
    printf("Fines:        check_overdue  view_fines  settle_fine\n");
    printf("Pool/Mining:  pool_view  mine_solo  mine_pool  mine_cloud <rounds>\n");
    printf("              difficulty_status  set_difficulty\n");
    printf("Chain:        chain_view  validate_chain  chain_save  chain_load  tamper_block  replay_test\n");
    printf("Tokens:       utxo_view  token_transfer  token_balance  credit_test_utxo\n");
    printf("              account_balance  account_transfer  account_nonce\n");
    printf("              transaction_history <member_id>  credit_test_account\n");
    printf("System:       help  exit\n");
    printf("> ");
}

/* A copy's current loan status is decided by whichever record is more
 * recent: an unconfirmed one still sitting in the pending pool, or the
 * last confirmed one on the chain. The pending pool is checked first
 * since anything in it is, by definition, newer than the chain tip. */
static const LendingRecord *current_loan_status(const PendingPool *pool, const Blockchain *chain,
                                                  const char *copy_id) {
    const LendingRecord *pending = pending_pool_last_action_for_copy(pool, copy_id);
    if (pending != NULL) return pending;
    return blockchain_last_action_for_copy(chain, copy_id);
}

/* record_id just needs to be unique across the chain and pool, not
 * sequential or gap-free, so the simplest honest source is a counter
 * seeded from how many records already exist in both places. */
static unsigned long next_record_id(const Blockchain *chain, const PendingPool *pool) {
    unsigned long count = (unsigned long)pool->count;
    for (int i = 0; i < chain->count; i++) {
        count += (unsigned long)chain->blocks[i].record_count;
    }
    return count + 1;
}

/* The registry's nonce only ever advances on confirmation, so using it
 * directly for every new request would hand out the same nonce to a
 * member's second action if their first is still unconfirmed. A new
 * request's nonce is instead the confirmed nonce plus however many of
 * that member's requests are already queued ahead of it in the pool -
 * the same "confirmed nonce + pending count" tracking a real wallet
 * does locally, so sequential actions from one member don't collide
 * with each other while the authoritative confirmed value still only
 * moves at mining time. */
static unsigned long next_assignable_nonce(const PendingPool *pool, NonceRegistry *nonces,
                                            const char *member_id) {
    unsigned long n = nonce_registry_get(nonces, member_id);
    for (int i = 0; i < pool->count; i++) {
        if (strcmp(pool->entries[i].member_id, member_id) == 0) {
            n++;
        }
    }
    return n;
}

static void save_inventory_state(const CopyInventory *inv, const ReservationQueue *q) {
    storage_save_copy_inventory(inv, COPY_STATE_FILE);
    storage_save_reservation_queue(q, RESERVATION_FILE);
}

static void handle_register_member(Registry *reg, MemberKeyStore *keys) {
    char member_id[20], full_name[50], course_code[10];
    printf("Member ID: ");
    read_line(member_id, sizeof(member_id));
    printf("Full Name: ");
    read_line(full_name, sizeof(full_name));
    printf("Course Code: ");
    read_line(course_code, sizeof(course_code));

    if (registry_find_member(reg, member_id) != NULL) {
        printf("ERROR: Member ID '%s' already exists\n", member_id);
        return;
    }
    if (reg->member_count >= MAX_MEMBERS) {
        printf("ERROR: Member registry is full\n");
        return;
    }

    Member *m = &reg->members[reg->member_count];
    strncpy(m->member_id, member_id, sizeof(m->member_id) - 1);
    strncpy(m->full_name, full_name, sizeof(m->full_name) - 1);
    strncpy(m->course_code, course_code, sizeof(m->course_code) - 1);
    reg->member_count++;

    /* Generating the keypair immediately (rather than waiting for their
     * first request) means a brand new member already has an on-chain
     * identity the moment they're registered. */
    if (member_key_store_get(keys, member_id) == NULL) {
        printf("WARNING: Member registered, but keypair generation failed\n");
        return;
    }
    printf("OK: Registered %s (%s), course %s, with a new signing keypair\n",
           full_name, member_id, course_code);
}

static void handle_view_member(const Registry *reg) {
    char member_id[20];
    printf("Member ID: ");
    read_line(member_id, sizeof(member_id));
    const Member *m = registry_find_member(reg, member_id);
    if (m == NULL) {
        printf("ERROR: Member '%s' not found\n", member_id);
        return;
    }
    printf("  %s: %s, course %s\n", m->member_id, m->full_name, m->course_code);
}

static void handle_member_nonce(NonceRegistry *nonces) {
    char member_id[20];
    printf("Member ID: ");
    read_line(member_id, sizeof(member_id));
    printf("  %s: confirmed nonce %lu\n", member_id, nonce_registry_get(nonces, member_id));
}

static void handle_register_book(Registry *reg) {
    char book_id[20], title[80], author[50];
    printf("Book ID: ");
    read_line(book_id, sizeof(book_id));
    printf("Title: ");
    read_line(title, sizeof(title));
    printf("Author: ");
    read_line(author, sizeof(author));

    if (registry_find_book(reg, book_id) != NULL) {
        printf("ERROR: Book ID '%s' already exists\n", book_id);
        return;
    }
    if (reg->book_count >= MAX_BOOKS) {
        printf("ERROR: Book registry is full\n");
        return;
    }

    Book *b = &reg->books[reg->book_count];
    strncpy(b->book_id, book_id, sizeof(b->book_id) - 1);
    strncpy(b->title, title, sizeof(b->title) - 1);
    strncpy(b->author, author, sizeof(b->author) - 1);
    reg->book_count++;
    printf("OK: Registered '%s' by %s (%s)\n", title, author, book_id);
}

static void handle_view_book(const Registry *reg) {
    char book_id[20];
    printf("Book ID: ");
    read_line(book_id, sizeof(book_id));
    const Book *b = registry_find_book(reg, book_id);
    if (b == NULL) {
        printf("ERROR: Book '%s' not found\n", book_id);
        return;
    }
    printf("  %s: '%s' by %s\n", b->book_id, b->title, b->author);
}

static void handle_add_copy(CopyInventory *inv, ReservationQueue *q, const Registry *reg) {
    char copy_id[20], book_id[20], condition[10];
    printf("Copy ID: ");
    read_line(copy_id, sizeof(copy_id));
    printf("Book ID: ");
    read_line(book_id, sizeof(book_id));
    printf("Condition (NEW/GOOD/WORN): ");
    read_line(condition, sizeof(condition));

    if (registry_find_book(reg, book_id) == NULL) {
        printf("ERROR: Book '%s' not found\n", book_id);
        return;
    }
    if (copy_inventory_find(inv, copy_id) != NULL) {
        printf("ERROR: Copy ID '%s' already exists\n", copy_id);
        return;
    }

    Copy c = {0};
    strncpy(c.copy_id, copy_id, sizeof(c.copy_id) - 1);
    strncpy(c.book_id, book_id, sizeof(c.book_id) - 1);
    strncpy(c.condition, condition, sizeof(c.condition) - 1);
    strncpy(c.state, COPY_STATE_AVAILABLE, sizeof(c.state) - 1);

    /* Reuses the inventory's own growable array by going through find()
     * after a manual append would be circular, so this mirrors
     * copy_inventory_load's insertion instead. */
    if (inv->count >= inv->capacity) {
        inv->capacity *= 2;
        inv->copies = realloc(inv->copies, sizeof(Copy) * (size_t)inv->capacity);
    }
    inv->copies[inv->count] = c;
    inv->count++;

    save_inventory_state(inv, q);
    printf("OK: Added copy '%s' of '%s', condition %s, state AVAILABLE\n", copy_id, book_id, condition);
}

static void handle_view_copy(CopyInventory *inv) {
    char copy_id[20];
    printf("Copy ID (blank for all): ");
    read_line(copy_id, sizeof(copy_id));
    if (copy_id[0] == '\0') {
        copy_inventory_print(inv);
        return;
    }
    Copy *c = copy_inventory_find(inv, copy_id);
    if (c == NULL) {
        printf("ERROR: Copy '%s' not found\n", copy_id);
        return;
    }
    copy_inventory_print_one(c);
}

static void handle_mark_lost(CopyInventory *inv, ReservationQueue *q, const char *arg) {
    char copy_id[20], reason[80];
    if (arg != NULL) {
        strncpy(copy_id, arg, sizeof(copy_id) - 1);
        copy_id[sizeof(copy_id) - 1] = '\0';
    } else {
        printf("Copy ID: ");
        read_line(copy_id, sizeof(copy_id));
    }
    printf("Reason code: ");
    read_line(reason, sizeof(reason));

    if (reason[0] == '\0') {
        printf("ERROR: A reason code is required for a LOST override\n");
        return;
    }

    Copy *c = copy_inventory_find(inv, copy_id);
    if (c == NULL) {
        printf("ERROR: Copy '%s' not found\n", copy_id);
        return;
    }
    if (strcmp(c->state, COPY_STATE_AVAILABLE) != 0 && strcmp(c->state, COPY_STATE_BORROWED) != 0) {
        printf("ERROR: Copy '%s' is already %s, cannot mark lost\n", copy_id, c->state);
        return;
    }

    strncpy(c->state, COPY_STATE_LOST, sizeof(c->state) - 1);
    c->state[sizeof(c->state) - 1] = '\0';
    save_inventory_state(inv, q);
    printf("OK: Copy '%s' marked LOST (reason: %s)\n", copy_id, reason);
}

static void handle_borrow(PendingPool *pool, const Blockchain *chain, const Registry *reg,
                           CopyInventory *inv, ReservationQueue *reservations,
                           MemberKeyStore *keys, NonceRegistry *nonces) {
    char book_id[20], member_id[20];

    printf("Enter Book ID: ");
    read_line(book_id, sizeof(book_id));
    printf("Enter Member ID: ");
    read_line(member_id, sizeof(member_id));

    const Book *book = registry_find_book(reg, book_id);
    const Member *member = registry_find_member(reg, member_id);

    if (book == NULL || member == NULL) {
        printf("ERROR: Book or Member not found\n");
        return;
    }

    /* A member picking up their own reservation claims that exact copy;
     * otherwise any generally-available copy of the title is offered. */
    Copy *copy = copy_inventory_find_reserved_for(inv, book_id, member_id);
    if (copy == NULL) {
        copy = copy_inventory_find_available(inv, book_id);
    }
    if (copy == NULL) {
        printf("ERROR: No available copy of '%s' - use reserve_book to join the queue\n", book->title);
        return;
    }

    LendingRecord rec = {0};
    rec.record_id = next_record_id(chain, pool);
    strncpy(rec.book_id, book->book_id, sizeof(rec.book_id) - 1);
    strncpy(rec.copy_id, copy->copy_id, sizeof(rec.copy_id) - 1);
    strncpy(rec.book_title, book->title, sizeof(rec.book_title) - 1);
    strncpy(rec.member_id, member->member_id, sizeof(rec.member_id) - 1);
    strncpy(rec.member_name, member->full_name, sizeof(rec.member_name) - 1);
    strncpy(rec.action, ACTION_BORROWED, sizeof(rec.action) - 1);
    rec.timestamp = time(NULL);
    rec.due_date = rec.timestamp + LOAN_PERIOD_SECONDS;
    rec.fine_amount = 0;
    rec.nonce = next_assignable_nonce(pool, nonces, member->member_id);

    if (!blockchain_sign_record(&rec, member_key_store_get(keys, member->member_id))) {
        printf("ERROR: Failed to sign borrow request\n");
        return;
    }

    pending_pool_add(pool, &rec);
    storage_save_pending_pool(pool, POOL_FILE);

    strncpy(copy->state, COPY_STATE_BORROWED, sizeof(copy->state) - 1);
    copy->state[sizeof(copy->state) - 1] = '\0';
    strncpy(copy->current_holder_id, member->member_id, sizeof(copy->current_holder_id) - 1);
    copy->current_holder_id[sizeof(copy->current_holder_id) - 1] = '\0';
    copy->due_date = rec.due_date;
    save_inventory_state(inv, reservations);

    printf("OK: '%s' (copy %s) borrow by %s added to the pending pool - awaiting mining.\n",
           book->title, copy->copy_id, member->full_name);
}

static void handle_return(PendingPool *pool, const Blockchain *chain, CopyInventory *inv,
                           ReservationQueue *reservations, MemberKeyStore *keys, NonceRegistry *nonces) {
    char copy_id[20];
    printf("Enter Copy ID: ");
    read_line(copy_id, sizeof(copy_id));

    Copy *copy = copy_inventory_find(inv, copy_id);
    if (copy == NULL) {
        printf("ERROR: Copy '%s' is not in the inventory\n", copy_id);
        return;
    }
    /* The copy's own state is the authority on whether it's on loan -
     * the last lending record's action might be RENEWED rather than
     * BORROWED if it's been renewed since, which still means it's out. */
    if (strcmp(copy->state, COPY_STATE_BORROWED) != 0) {
        printf("ERROR: Copy '%s' is not currently on loan (state: %s)\n", copy_id, copy->state);
        return;
    }

    const LendingRecord *last = current_loan_status(pool, chain, copy_id);

    LendingRecord rec = {0};
    rec.record_id = next_record_id(chain, pool);
    strncpy(rec.book_id, last->book_id, sizeof(rec.book_id) - 1);
    strncpy(rec.copy_id, last->copy_id, sizeof(rec.copy_id) - 1);
    strncpy(rec.book_title, last->book_title, sizeof(rec.book_title) - 1);
    strncpy(rec.member_id, last->member_id, sizeof(rec.member_id) - 1);
    strncpy(rec.member_name, last->member_name, sizeof(rec.member_name) - 1);
    strncpy(rec.action, ACTION_RETURNED, sizeof(rec.action) - 1);
    rec.timestamp = time(NULL);
    rec.due_date = last->due_date;
    rec.nonce = next_assignable_nonce(pool, nonces, last->member_id);

    /* A late return gets both a reduced token reward (handled at mining
     * time) and a fine, calculated per simulated day late - rounding up,
     * so even a few seconds into the next day counts as a full day. */
    if (rec.timestamp > rec.due_date) {
        long seconds_late = (long)(rec.timestamp - rec.due_date);
        long days_late = (seconds_late + SIMULATED_DAY_SECONDS - 1) / SIMULATED_DAY_SECONDS;
        rec.fine_amount = (int)days_late * FINE_PER_DAY;
    } else {
        rec.fine_amount = 0;
    }

    if (!blockchain_sign_record(&rec, member_key_store_get(keys, last->member_id))) {
        printf("ERROR: Failed to sign return request\n");
        return;
    }

    pending_pool_add(pool, &rec);
    storage_save_pending_pool(pool, POOL_FILE);

    /* BORROWED -> AVAILABLE, or RESERVED (held for the next member in
     * line) if anyone is waiting for this title. */
    char fine_note[32] = "";
    if (rec.fine_amount > 0) {
        snprintf(fine_note, sizeof(fine_note), ", fine %d", rec.fine_amount);
    }

    char next_member[20];
    if (reservation_queue_pop_next(reservations, copy->book_id, next_member)) {
        strncpy(copy->state, COPY_STATE_RESERVED, sizeof(copy->state) - 1);
        strncpy(copy->current_holder_id, next_member, sizeof(copy->current_holder_id) - 1);
        printf("OK: '%s' return by %s added to the pending pool - awaiting mining (%s%s). Copy %s now held for %s.\n",
               last->book_title, last->member_name,
               rec.timestamp <= last->due_date ? "on-time" : "late", fine_note, copy_id, next_member);
    } else {
        strncpy(copy->state, COPY_STATE_AVAILABLE, sizeof(copy->state) - 1);
        copy->current_holder_id[0] = '\0';
        printf("OK: '%s' return by %s added to the pending pool - awaiting mining (%s%s, reward decided on confirmation).\n",
               last->book_title, last->member_name,
               rec.timestamp <= last->due_date ? "on-time" : "late", fine_note);
    }
    copy->state[sizeof(copy->state) - 1] = '\0';
    copy->current_holder_id[sizeof(copy->current_holder_id) - 1] = '\0';
    copy->due_date = 0;
    copy->renewal_count = 0;
    save_inventory_state(inv, reservations);
}

static void handle_renew_book(PendingPool *pool, const Blockchain *chain, CopyInventory *inv,
                               ReservationQueue *reservations, MemberKeyStore *keys, NonceRegistry *nonces) {
    char copy_id[20];
    printf("Enter Copy ID: ");
    read_line(copy_id, sizeof(copy_id));

    Copy *copy = copy_inventory_find(inv, copy_id);
    if (copy == NULL) {
        printf("ERROR: Copy '%s' is not in the inventory\n", copy_id);
        return;
    }
    if (strcmp(copy->state, COPY_STATE_BORROWED) != 0) {
        printf("ERROR: Copy '%s' is not currently on loan (state: %s)\n", copy_id, copy->state);
        return;
    }
    if (reservation_queue_has_waiting(reservations, copy->book_id)) {
        printf("ERROR: Another member is waiting for '%s' - cannot renew\n", copy->book_id);
        return;
    }
    if (copy->renewal_count >= MAX_RENEWALS) {
        printf("ERROR: Copy '%s' has already been renewed the maximum %d time(s)\n", copy_id, MAX_RENEWALS);
        return;
    }

    const LendingRecord *last = current_loan_status(pool, chain, copy_id);

    LendingRecord rec = {0};
    rec.record_id = next_record_id(chain, pool);
    strncpy(rec.book_id, last->book_id, sizeof(rec.book_id) - 1);
    strncpy(rec.copy_id, last->copy_id, sizeof(rec.copy_id) - 1);
    strncpy(rec.book_title, last->book_title, sizeof(rec.book_title) - 1);
    strncpy(rec.member_id, last->member_id, sizeof(rec.member_id) - 1);
    strncpy(rec.member_name, last->member_name, sizeof(rec.member_name) - 1);
    strncpy(rec.action, ACTION_RENEWED, sizeof(rec.action) - 1);
    rec.timestamp = time(NULL);
    rec.due_date = last->due_date + RENEWAL_PERIOD_SECONDS;
    rec.fine_amount = 0;
    rec.nonce = next_assignable_nonce(pool, nonces, last->member_id);

    if (!blockchain_sign_record(&rec, member_key_store_get(keys, last->member_id))) {
        printf("ERROR: Failed to sign renewal request\n");
        return;
    }

    pending_pool_add(pool, &rec);
    storage_save_pending_pool(pool, POOL_FILE);

    copy->due_date = rec.due_date;
    copy->renewal_count++;
    save_inventory_state(inv, reservations);

    printf("OK: Copy '%s' renewal %d/%d added to the pending pool - new due date pending confirmation.\n",
           copy_id, copy->renewal_count, MAX_RENEWALS);
}

static void handle_reserve_book(const Registry *reg, CopyInventory *inv, ReservationQueue *reservations) {
    char book_id[20], member_id[20];
    printf("Book ID: ");
    read_line(book_id, sizeof(book_id));
    printf("Member ID: ");
    read_line(member_id, sizeof(member_id));

    if (registry_find_book(reg, book_id) == NULL || registry_find_member(reg, member_id) == NULL) {
        printf("ERROR: Book or Member not found\n");
        return;
    }
    if (copy_inventory_find_available(inv, book_id) != NULL) {
        printf("ERROR: A copy of '%s' is currently available - borrow it directly instead\n", book_id);
        return;
    }

    reservation_queue_add(reservations, book_id, member_id);
    save_inventory_state(inv, reservations);
    printf("OK: %s added to the reservation queue for '%s'\n", member_id, book_id);
}

static void handle_reservation_status(const ReservationQueue *reservations) {
    reservation_queue_print(reservations);
}

/* Scans every currently-BORROWED copy for a due_date that's passed, and
 * either creates a new pending OVERDUE record or - if that copy is
 * already flagged and still sitting unconfirmed in the pool - updates
 * the fine in place, since more simulated days may have passed since it
 * was first flagged. Re-signing after mutating a pending record is
 * required: the signature covers fine_amount and timestamp, both of
 * which just changed. */
static void handle_check_overdue(PendingPool *pool, const Blockchain *chain, CopyInventory *inv,
                                  MemberKeyStore *keys, NonceRegistry *nonces) {
    time_t now = time(NULL);
    int flagged = 0;

    for (int i = 0; i < inv->count; i++) {
        Copy *c = &inv->copies[i];
        if (strcmp(c->state, COPY_STATE_BORROWED) != 0) continue;
        if (c->due_date == 0 || now <= c->due_date) continue;

        long seconds_late = (long)(now - c->due_date);
        long days_late = (seconds_late + SIMULATED_DAY_SECONDS - 1) / SIMULATED_DAY_SECONDS;
        int fine = (int)days_late * FINE_PER_DAY;

        LendingRecord *existing = NULL;
        for (int j = 0; j < pool->count; j++) {
            if (strcmp(pool->entries[j].copy_id, c->copy_id) == 0 &&
                strcmp(pool->entries[j].action, ACTION_OVERDUE) == 0) {
                existing = &pool->entries[j];
                break;
            }
        }

        if (existing != NULL) {
            if (fine != existing->fine_amount) {
                existing->fine_amount = fine;
                existing->timestamp = now;
                blockchain_sign_record(existing, member_key_store_get(keys, existing->member_id));
                flagged++;
            }
            continue;
        }

        /* The copy's state already confirms it's on loan (checked at the
         * top of this loop) - last may be a BORROWED or RENEWED record,
         * either way it carries the book_title/member_name needed below. */
        const LendingRecord *last = current_loan_status(pool, chain, c->copy_id);
        if (last == NULL) continue;

        LendingRecord rec = {0};
        rec.record_id = next_record_id(chain, pool);
        strncpy(rec.book_id, last->book_id, sizeof(rec.book_id) - 1);
        strncpy(rec.copy_id, c->copy_id, sizeof(rec.copy_id) - 1);
        strncpy(rec.book_title, last->book_title, sizeof(rec.book_title) - 1);
        strncpy(rec.member_id, c->current_holder_id, sizeof(rec.member_id) - 1);
        strncpy(rec.member_name, last->member_name, sizeof(rec.member_name) - 1);
        strncpy(rec.action, ACTION_OVERDUE, sizeof(rec.action) - 1);
        rec.timestamp = now;
        rec.due_date = c->due_date;
        rec.fine_amount = fine;
        rec.nonce = next_assignable_nonce(pool, nonces, c->current_holder_id);

        if (blockchain_sign_record(&rec, member_key_store_get(keys, c->current_holder_id))) {
            pending_pool_add(pool, &rec);
            flagged++;
        }
    }

    if (flagged > 0) {
        storage_save_pending_pool(pool, POOL_FILE);
        printf("Flagged or updated %d overdue copy(ies) - fines pending confirmation.\n", flagged);
    } else {
        printf("No overdue copies found.\n");
    }
}

static void handle_view_fines(const FineLedger *fines) {
    fine_ledger_print(fines);
}

static void handle_settle_fine(FineLedger *fines) {
    char member_id[20], input[16];
    printf("Member ID: ");
    read_line(member_id, sizeof(member_id));
    printf("Amount to settle: ");
    read_line(input, sizeof(input));
    int amount = atoi(input);

    if (!fine_ledger_settle(fines, member_id, amount)) {
        printf("ERROR: '%s' does not owe at least %d\n", member_id, amount);
        return;
    }

    storage_save_fine_ledger(fines, FINE_FILE);
    printf("OK: Settled %d for %s (remaining owed: %d)\n",
           amount, member_id, fine_ledger_owed(fines, member_id));
}

static void save_ledgers(TransactionModel model, UTXOSet *utxos, AccountLedger *accounts) {
    if (model == MODEL_UTXO) {
        storage_save_utxo_set(utxos, UTXO_FILE);
    } else {
        storage_save_account_ledger(accounts, ACCOUNT_FILE);
    }
}

/* The brief requires the full UTXO set to be shown after each confirmed
 * block, so mining prints the active ledger's state automatically instead
 * of leaving it to a separate command the user might not run. */
static void print_ledger_state(TransactionModel model, UTXOSet *utxos, AccountLedger *accounts) {
    if (model == MODEL_UTXO) {
        utxo_print_set(utxos);
    } else {
        account_print_all(accounts);
    }
}

static void handle_mine_solo(Blockchain *chain, PendingPool *pool, int difficulty,
                              TransactionModel model, UTXOSet *utxos, AccountLedger *accounts,
                              FineLedger *fines, NonceRegistry *nonces) {
    char miner_id[50];
    printf("Enter Miner ID: ");
    read_line(miner_id, sizeof(miner_id));
    if (miner_id[0] == '\0') {
        strcpy(miner_id, "MINER1");
    }

    mining_solo(chain, pool, difficulty, miner_id, model, utxos, accounts, fines, nonces);

    storage_save_chain(chain, CHAIN_FILE);
    storage_save_pending_pool(pool, POOL_FILE);
    save_ledgers(model, utxos, accounts);
    storage_save_fine_ledger(fines, FINE_FILE);
    storage_save_nonce_registry(nonces, NONCE_FILE);
    print_ledger_state(model, utxos, accounts);
}

static void handle_mine_pool(Blockchain *chain, PendingPool *pool, int difficulty,
                              TransactionModel model, UTXOSet *utxos, AccountLedger *accounts,
                              FineLedger *fines, NonceRegistry *nonces) {
    char input[16];
    printf("Number of pool miners (%d-%d, default %d): ", 1, MAX_POOL_MINERS, DEFAULT_POOL_MINERS);
    read_line(input, sizeof(input));
    int num_miners = input[0] == '\0' ? DEFAULT_POOL_MINERS : atoi(input);

    PoolMinerResult results[MAX_POOL_MINERS];
    mining_pool(chain, pool, difficulty, num_miners, model, utxos, accounts, fines, nonces, results);

    storage_save_chain(chain, CHAIN_FILE);
    storage_save_pending_pool(pool, POOL_FILE);
    save_ledgers(model, utxos, accounts);
    storage_save_fine_ledger(fines, FINE_FILE);
    storage_save_nonce_registry(nonces, NONCE_FILE);
    print_ledger_state(model, utxos, accounts);
}

static void handle_mine_cloud(Blockchain *chain, PendingPool *pool, int difficulty,
                               TransactionModel model, UTXOSet *utxos, AccountLedger *accounts,
                               FineLedger *fines, NonceRegistry *nonces, const char *arg) {
    char renter_id[50], input[16];
    printf("Enter Renter ID: ");
    read_line(renter_id, sizeof(renter_id));
    if (renter_id[0] == '\0') {
        strcpy(renter_id, "CLOUD_RENTER");
    }

    int rounds;
    if (arg != NULL) {
        rounds = atoi(arg);
    } else {
        printf("Number of rounds (%d-%d): ", MIN_CLOUD_ROUNDS, MAX_CLOUD_ROUNDS);
        read_line(input, sizeof(input));
        rounds = atoi(input);
    }

    CloudRound rounds_out[MAX_CLOUD_ROUNDS];
    mining_cloud(chain, pool, difficulty, rounds, renter_id, model, utxos, accounts, fines, nonces, rounds_out);

    storage_save_chain(chain, CHAIN_FILE);
    storage_save_pending_pool(pool, POOL_FILE);
    save_ledgers(model, utxos, accounts);
    storage_save_fine_ledger(fines, FINE_FILE);
    storage_save_nonce_registry(nonces, NONCE_FILE);
    print_ledger_state(model, utxos, accounts);
}

static int handle_set_difficulty(int current) {
    printf("Enter new difficulty (%d-%d): ", MIN_DIFFICULTY, MAX_DIFFICULTY);
    char input[16];
    read_line(input, sizeof(input));
    int value = atoi(input);

    if (value < MIN_DIFFICULTY || value > MAX_DIFFICULTY) {
        printf("ERROR: Difficulty must be between %d and %d. Keeping current value (%d).\n",
               MIN_DIFFICULTY, MAX_DIFFICULTY, current);
        return current;
    }

    printf("Difficulty set to %d.\n", value);
    return value;
}

static void handle_utxo_send(UTXOSet *utxos) {
    char from[20], to[20], input[16];
    printf("From (member ID): ");
    read_line(from, sizeof(from));
    printf("To (member ID): ");
    read_line(to, sizeof(to));
    printf("Amount: ");
    read_line(input, sizeof(input));
    int amount = atoi(input);

    if (amount <= 0) {
        printf("ERROR: Amount must be positive\n");
        return;
    }

    char txid[HASH_HEX_LEN];
    crypto_compute_transaction_id(from, to, amount, time(NULL), txid);
    if (!utxo_transfer(utxos, from, to, amount, NETWORK_ACCOUNT, txid)) {
        printf("ERROR: '%s' does not have enough unspent balance to send %d (+%d fee)\n",
               from, amount, UTXO_FEE);
        return;
    }

    storage_save_utxo_set(utxos, UTXO_FILE);
    printf("OK: %d coins sent from %s to %s (fee: %d to %s). txid %.16s...\n",
           amount, from, to, UTXO_FEE, NETWORK_ACCOUNT, txid);
}

static void handle_token_balance(UTXOSet *utxos) {
    char member_id[20];
    printf("Member ID: ");
    read_line(member_id, sizeof(member_id));
    printf("  %s: %d coins\n", member_id, utxo_balance(utxos, member_id));
}

/* A manual test utility for crediting an arbitrary amount outside the
 * normal borrow/return/mine flow - useful for setting up balances to
 * demo token_transfer or edge cases without waiting on a real loan cycle. */
static void handle_utxo_credit_test(UTXOSet *utxos) {
    char member_id[20], input[16];
    printf("Credit which member ID: ");
    read_line(member_id, sizeof(member_id));
    printf("Reward amount (e.g. 10 on-time, 5 late): ");
    read_line(input, sizeof(input));
    int amount = atoi(input);

    char txid[HASH_HEX_LEN];
    crypto_compute_transaction_id("SYSTEM", member_id, amount, time(NULL), txid);
    if (!utxo_mint(utxos, member_id, amount, NETWORK_ACCOUNT, txid)) {
        printf("ERROR: Reward amount must be greater than the %d-coin fee\n", UTXO_FEE);
        return;
    }

    storage_save_utxo_set(utxos, UTXO_FILE);
    printf("OK: %d coins minted for %s (%d fee to %s). txid %.16s...\n",
           amount, member_id, UTXO_FEE, NETWORK_ACCOUNT, txid);
}

static void handle_account_send(AccountLedger *ledger) {
    char from[20], to[20], input[16];
    printf("From (member ID): ");
    read_line(from, sizeof(from));

    unsigned long expected = account_get_or_create(ledger, from)->nonce;
    printf("To (member ID): ");
    read_line(to, sizeof(to));
    printf("Amount: ");
    read_line(input, sizeof(input));
    int amount = atoi(input);
    printf("Nonce (expected: %lu): ", expected);
    read_line(input, sizeof(input));
    unsigned long nonce = (unsigned long)strtoul(input, NULL, 10);

    if (amount <= 0) {
        printf("ERROR: Amount must be positive\n");
        return;
    }

    AccountResult result = account_transfer(ledger, from, to, amount, nonce, NETWORK_ACCOUNT);
    switch (result) {
        case ACCOUNT_OK:
            storage_save_account_ledger(ledger, ACCOUNT_FILE);
            printf("OK: %d coins sent from %s to %s (fee: %d to %s, nonce %lu used).\n",
                   amount, from, to, ACCOUNT_FEE, NETWORK_ACCOUNT, nonce);
            break;
        case ACCOUNT_ERR_BAD_NONCE:
            printf("ERROR: Incorrect or reused nonce (expected %lu, got %lu)\n", expected, nonce);
            break;
        case ACCOUNT_ERR_INSUFFICIENT_BALANCE:
            printf("ERROR: '%s' does not have enough balance to send %d (+%d fee)\n",
                   from, amount, ACCOUNT_FEE);
            break;
    }
}

static void handle_account_nonce(AccountLedger *ledger) {
    char member_id[20];
    printf("Member ID: ");
    read_line(member_id, sizeof(member_id));
    printf("  %s: next nonce %lu\n", member_id, account_get_or_create(ledger, member_id)->nonce);
}

static void handle_account_history(AccountLedger *ledger, const char *arg) {
    char member_id[20];
    if (arg != NULL) {
        strncpy(member_id, arg, sizeof(member_id) - 1);
        member_id[sizeof(member_id) - 1] = '\0';
    } else {
        printf("Member ID: ");
        read_line(member_id, sizeof(member_id));
    }
    account_print_history(ledger, member_id);
}

/* A manual test utility, same purpose as handle_utxo_credit_test but for
 * the account model. */
static void handle_account_credit_test(AccountLedger *ledger) {
    char member_id[20], input[16];
    printf("Credit which member ID: ");
    read_line(member_id, sizeof(member_id));
    printf("Reward amount (e.g. 10 on-time, 5 late): ");
    read_line(input, sizeof(input));
    int amount = atoi(input);

    if (!account_credit_reward(ledger, member_id, amount, NETWORK_ACCOUNT)) {
        printf("ERROR: Reward amount must be greater than the %d-coin fee\n", ACCOUNT_FEE);
        return;
    }

    storage_save_account_ledger(ledger, ACCOUNT_FILE);
    printf("OK: %d coins credited to %s (%d fee to %s).\n",
           amount, member_id, ACCOUNT_FEE, NETWORK_ACCOUNT);
}

/* A dev/test hook, same purpose as credit_test_utxo/credit_test_account:
 * nonce rejection is designed to never trigger during normal legitimate
 * use (next_assignable_nonce already prevents a member's own requests
 * from colliding), so the only way to actually show it happening is to
 * simulate what an attacker would do - replay an already-confirmed,
 * genuinely validly-signed record back into the pool with its now-stale
 * nonce. The signature still verifies fine (nothing about the record
 * itself changed), which is exactly why the nonce check, not the
 * signature check, is what has to catch this. */
static void handle_replay_test(PendingPool *pool, const Blockchain *chain, const char *arg) {
    char copy_id[20];
    if (arg != NULL) {
        strncpy(copy_id, arg, sizeof(copy_id) - 1);
        copy_id[sizeof(copy_id) - 1] = '\0';
    } else {
        printf("Copy ID (of an already-confirmed record): ");
        read_line(copy_id, sizeof(copy_id));
    }

    const LendingRecord *last = blockchain_last_action_for_copy(chain, copy_id);
    if (last == NULL) {
        printf("ERROR: No confirmed record found for copy '%s'\n", copy_id);
        return;
    }

    pending_pool_add(pool, last);
    storage_save_pending_pool(pool, POOL_FILE);
    printf("OK: Replayed the confirmed %s record for copy '%s' (member %s, nonce %lu) back into the pool.\n",
           last->action, copy_id, last->member_id, last->nonce);
    printf("Run mine_solo/mine_pool/mine_cloud now - it should be rejected, since that nonce is already spent.\n");
}

static void handle_tamper_demo(Blockchain *chain) {
    int tamperable = 0;
    for (int i = 1; i < chain->count; i++) {
        if (chain->blocks[i].record_count > 0) tamperable = 1;
    }
    if (!tamperable) {
        printf("No lending records yet to tamper with (only the genesis block exists).\n");
        return;
    }

    printf("Enter the index of the block to tamper with (1-%d): ", chain->count - 1);
    char input[16];
    read_line(input, sizeof(input));
    int idx = atoi(input);

    if (idx < 1 || idx >= chain->count || chain->blocks[idx].record_count == 0) {
        printf("ERROR: Invalid block index\n");
        return;
    }

    Block *b = &chain->blocks[idx];
    LendingRecord *rec = &b->records[0];
    printf("Tampering with block %d, record 0: book_title '%s' -> 'TAMPERED RECORD' (without re-signing, hash/merkle root left stale)\n",
           idx, rec->book_title);
    strncpy(rec->book_title, "TAMPERED RECORD", sizeof(rec->book_title) - 1);
    rec->book_title[sizeof(rec->book_title) - 1] = '\0';

    storage_save_chain(chain, CHAIN_FILE);
    printf("Block %d modified in memory and on disk. Run 'validate_chain' to see it detected.\n", idx);
}

int main(int argc, char *argv[]) {
    int difficulty = DEFAULT_DIFFICULTY;
    TransactionModel model = MODEL_UTXO;
    int model_set_by_arg = 0;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--difficulty") == 0) {
            int value = atoi(argv[i + 1]);
            if (value >= MIN_DIFFICULTY && value <= MAX_DIFFICULTY) {
                difficulty = value;
            } else {
                fprintf(stderr, "WARNING: --difficulty must be %d-%d, ignoring '%s'\n",
                        MIN_DIFFICULTY, MAX_DIFFICULTY, argv[i + 1]);
            }
        } else if (strcmp(argv[i], "--model") == 0) {
            if (strcmp(argv[i + 1], "account") == 0) {
                model = MODEL_ACCOUNT;
                model_set_by_arg = 1;
            } else if (strcmp(argv[i + 1], "utxo") == 0) {
                model = MODEL_UTXO;
                model_set_by_arg = 1;
            } else {
                fprintf(stderr, "WARNING: --model must be 'utxo' or 'account', ignoring '%s'\n",
                        argv[i + 1]);
            }
        }
    }

    srand((unsigned int)time(NULL));

    Registry reg;
    if (!registry_load(&reg, BOOKS_FILE, MEMBERS_FILE)) {
        fprintf(stderr, "FATAL: Could not start — book/member registries missing or empty.\n");
        return 1;
    }
    printf("Loaded %d books and %d members.\n", reg.book_count, reg.member_count);

    /* Formative 1/2 used one shared librarian keypair to sign everything.
     * The summative requires every member to have their own identity, so
     * each member's key is generated lazily, the first time they're
     * looked up, and reused from keys/members/ on every later run. */
    MemberKeyStore member_keys;
    member_key_store_init(&member_keys);

    NonceRegistry nonces;
    nonce_registry_init(&nonces);
    storage_load_nonce_registry(&nonces, NONCE_FILE);

    Blockchain chain;
    blockchain_init(&chain);
    if (!storage_load_chain(&chain, CHAIN_FILE)) {
        printf("No existing chain found — creating genesis block.\n");
        blockchain_add_genesis(&chain);
        storage_save_chain(&chain, CHAIN_FILE);
    } else {
        printf("Loaded existing chain with %d block(s).\n", chain.count);
    }

    PendingPool pool;
    pending_pool_init(&pool);
    if (storage_load_pending_pool(&pool, POOL_FILE) && pool.count > 0) {
        printf("Loaded %d pending record(s) from a previous session.\n", pool.count);
    }

    UTXOSet utxos;
    utxo_set_init(&utxos);
    storage_load_utxo_set(&utxos, UTXO_FILE);

    AccountLedger accounts;
    account_ledger_init(&accounts);
    storage_load_account_ledger(&accounts, ACCOUNT_FILE);

    CopyInventory inv;
    copy_inventory_init(&inv);
    if (!storage_load_copy_inventory(&inv, COPY_STATE_FILE)) {
        if (!copy_inventory_load(&inv, COPIES_FILE, &reg)) {
            fprintf(stderr, "FATAL: Could not start — copy inventory missing or empty.\n");
            return 1;
        }
        printf("Loaded %d physical copies from %s.\n", inv.count, COPIES_FILE);
    } else {
        printf("Loaded existing copy inventory with %d copies.\n", inv.count);
    }

    ReservationQueue reservations;
    reservation_queue_init(&reservations);
    storage_load_reservation_queue(&reservations, RESERVATION_FILE);

    FineLedger fines;
    fine_ledger_init(&fines);
    storage_load_fine_ledger(&fines, FINE_FILE);

    if (!model_set_by_arg) {
        printf("\nChoose a transaction model for this session:\n");
        printf("  1. UTXO\n");
        printf("  2. Account-based\n");
        printf("Choice (default UTXO): ");
        char model_input[16];
        read_line(model_input, sizeof(model_input));
        model = (atoi(model_input) == 2) ? MODEL_ACCOUNT : MODEL_UTXO;
    }
    printf("Active transaction model: %s (applies to all token activity this session)\n",
           model_name(model));

    /* Overdue detection runs automatically on this CLI invocation (i.e.
     * each time the program starts), in addition to being callable
     * on-demand mid-session via check_overdue. */
    handle_check_overdue(&pool, &chain, &inv, &member_keys, &nonces);

    int running = 1;
    while (running) {
        print_commands(difficulty, model);
        char input[256];
        if (!read_line(input, sizeof(input))) {
            printf("\nInput closed - exiting.\n");
            break;
        }
        char *cmd, *arg;
        split_command(input, &cmd, &arg);

        if (strcmp(cmd, "") == 0) {
            continue;
        } else if (strcmp(cmd, "help") == 0) {
            continue;
        } else if (strcmp(cmd, "register_member") == 0) {
            handle_register_member(&reg, &member_keys);
        } else if (strcmp(cmd, "view_member") == 0) {
            handle_view_member(&reg);
        } else if (strcmp(cmd, "member_nonce") == 0) {
            handle_member_nonce(&nonces);
        } else if (strcmp(cmd, "register_book") == 0) {
            handle_register_book(&reg);
        } else if (strcmp(cmd, "view_book") == 0) {
            handle_view_book(&reg);
        } else if (strcmp(cmd, "add_copy") == 0) {
            handle_add_copy(&inv, &reservations, &reg);
        } else if (strcmp(cmd, "view_copy") == 0) {
            handle_view_copy(&inv);
        } else if (strcmp(cmd, "mark_lost") == 0) {
            handle_mark_lost(&inv, &reservations, arg);
        } else if (strcmp(cmd, "borrow_book") == 0) {
            handle_borrow(&pool, &chain, &reg, &inv, &reservations, &member_keys, &nonces);
        } else if (strcmp(cmd, "return_book") == 0) {
            handle_return(&pool, &chain, &inv, &reservations, &member_keys, &nonces);
        } else if (strcmp(cmd, "renew_book") == 0) {
            handle_renew_book(&pool, &chain, &inv, &reservations, &member_keys, &nonces);
        } else if (strcmp(cmd, "reserve_book") == 0) {
            handle_reserve_book(&reg, &inv, &reservations);
        } else if (strcmp(cmd, "reservation_status") == 0) {
            handle_reservation_status(&reservations);
        } else if (strcmp(cmd, "check_overdue") == 0) {
            handle_check_overdue(&pool, &chain, &inv, &member_keys, &nonces);
        } else if (strcmp(cmd, "view_fines") == 0) {
            handle_view_fines(&fines);
        } else if (strcmp(cmd, "settle_fine") == 0) {
            handle_settle_fine(&fines);
        } else if (strcmp(cmd, "pool_view") == 0) {
            pending_pool_print(&pool);
        } else if (strcmp(cmd, "mine_solo") == 0) {
            handle_mine_solo(&chain, &pool, difficulty, model, &utxos, &accounts, &fines, &nonces);
        } else if (strcmp(cmd, "mine_pool") == 0) {
            handle_mine_pool(&chain, &pool, difficulty, model, &utxos, &accounts, &fines, &nonces);
        } else if (strcmp(cmd, "mine_cloud") == 0) {
            handle_mine_cloud(&chain, &pool, difficulty, model, &utxos, &accounts, &fines, &nonces, arg);
        } else if (strcmp(cmd, "difficulty_status") == 0) {
            printf("Current mining difficulty: %d (range %d-%d)\n", difficulty, MIN_DIFFICULTY, MAX_DIFFICULTY);
        } else if (strcmp(cmd, "set_difficulty") == 0) {
            difficulty = handle_set_difficulty(difficulty);
        } else if (strcmp(cmd, "chain_view") == 0) {
            blockchain_print_records(&chain, &member_keys);
        } else if (strcmp(cmd, "validate_chain") == 0) {
            int valid = blockchain_validate(&chain, &member_keys);
            printf(valid ? "\nChain is VALID — no tampering detected.\n"
                         : "\nChain is INVALID — tampering detected!\n");
        } else if (strcmp(cmd, "chain_save") == 0) {
            storage_save_chain(&chain, CHAIN_FILE);
            printf("OK: Chain saved to %s\n", CHAIN_FILE);
        } else if (strcmp(cmd, "chain_load") == 0) {
            if (storage_load_chain(&chain, CHAIN_FILE)) {
                printf("OK: Chain reloaded from %s (%d blocks)\n", CHAIN_FILE, chain.count);
            } else {
                printf("ERROR: Could not load %s\n", CHAIN_FILE);
            }
        } else if (strcmp(cmd, "tamper_block") == 0) {
            handle_tamper_demo(&chain);
        } else if (strcmp(cmd, "replay_test") == 0) {
            handle_replay_test(&pool, &chain, arg);
        } else if (strcmp(cmd, "utxo_view") == 0) {
            utxo_print_set(&utxos);
        } else if (strcmp(cmd, "token_transfer") == 0) {
            if (model != MODEL_UTXO) {
                printf("ERROR: UTXO model is not active this session (active: %s)\n", model_name(model));
            } else {
                handle_utxo_send(&utxos);
            }
        } else if (strcmp(cmd, "token_balance") == 0) {
            handle_token_balance(&utxos);
        } else if (strcmp(cmd, "credit_test_utxo") == 0) {
            if (model != MODEL_UTXO) {
                printf("ERROR: UTXO model is not active this session (active: %s)\n", model_name(model));
            } else {
                handle_utxo_credit_test(&utxos);
            }
        } else if (strcmp(cmd, "account_balance") == 0) {
            account_print_all(&accounts);
        } else if (strcmp(cmd, "account_transfer") == 0) {
            if (model != MODEL_ACCOUNT) {
                printf("ERROR: Account model is not active this session (active: %s)\n", model_name(model));
            } else {
                handle_account_send(&accounts);
            }
        } else if (strcmp(cmd, "account_nonce") == 0) {
            handle_account_nonce(&accounts);
        } else if (strcmp(cmd, "transaction_history") == 0) {
            handle_account_history(&accounts, arg);
        } else if (strcmp(cmd, "credit_test_account") == 0) {
            if (model != MODEL_ACCOUNT) {
                printf("ERROR: Account model is not active this session (active: %s)\n", model_name(model));
            } else {
                handle_account_credit_test(&accounts);
            }
        } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            running = 0;
        } else {
            printf("Unknown command '%s'. Type 'help' to see the command list.\n", cmd);
        }
    }

    nonce_registry_free(&nonces);
    member_key_store_free(&member_keys);
    fine_ledger_free(&fines);
    reservation_queue_free(&reservations);
    copy_inventory_free(&inv);
    account_ledger_free(&accounts);
    utxo_set_free(&utxos);
    pending_pool_free(&pool);
    blockchain_free(&chain);
    return 0;
}
