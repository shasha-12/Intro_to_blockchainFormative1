#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utxo.h"

#define INITIAL_CAPACITY 32

void utxo_set_init(UTXOSet *set) {
    set->count = 0;
    set->capacity = INITIAL_CAPACITY;
    set->items = malloc(sizeof(UTXO) * (size_t)set->capacity);
}

void utxo_set_free(UTXOSet *set) {
    free(set->items);
    set->items = NULL;
    set->count = 0;
    set->capacity = 0;
}

static void ensure_capacity(UTXOSet *set) {
    if (set->count >= set->capacity) {
        set->capacity *= 2;
        set->items = realloc(set->items, sizeof(UTXO) * (size_t)set->capacity);
    }
}

static void add_output(UTXOSet *set, const char *txid, int output_index,
                        const char *owner, int amount) {
    if (amount <= 0) return;
    ensure_capacity(set);
    UTXO *u = &set->items[set->count];
    strncpy(u->txid, txid, sizeof(u->txid) - 1);
    u->txid[sizeof(u->txid) - 1] = '\0';
    u->output_index = output_index;
    strncpy(u->owner, owner, sizeof(u->owner) - 1);
    u->owner[sizeof(u->owner) - 1] = '\0';
    u->amount = amount;
    set->count++;
}

/* Physically removes the entry rather than marking it spent - this is what
 * makes double-spending structurally impossible: once a UTXO is consumed as
 * an input, it no longer exists anywhere in the set to be spent a second
 * time. */
static void remove_at(UTXOSet *set, int i) {
    for (int j = i; j < set->count - 1; j++) {
        set->items[j] = set->items[j + 1];
    }
    set->count--;
}

int utxo_balance(const UTXOSet *set, const char *owner) {
    int total = 0;
    for (int i = 0; i < set->count; i++) {
        if (strcmp(set->items[i].owner, owner) == 0) {
            total += set->items[i].amount;
        }
    }
    return total;
}

int utxo_credit_direct(UTXOSet *set, const char *owner, int amount, const char *txid) {
    if (amount <= 0) return 0;
    add_output(set, txid, 0, owner, amount);
    return 1;
}

/* Mints a reward as two brand-new outputs from the same txid: the member's
 * share minus the flat UTXO_FEE, and the fee itself to fee_owner. Rejected
 * outright if the reward can't even cover its own fee. */
int utxo_mint(UTXOSet *set, const char *to_owner, int amount,
              const char *fee_owner, const char *txid) {
    if (amount <= UTXO_FEE) {
        return 0;
    }

    add_output(set, txid, 0, to_owner, amount - UTXO_FEE);
    add_output(set, txid, 1, fee_owner, UTXO_FEE);
    return 1;
}

/* A member's "balance" doesn't exist as a stored number - it's whatever
 * their unspent outputs add up to. So spending means gathering whole
 * outputs as inputs until they cover amount + fee, consuming all of them
 * entirely (no partial spends), and - if they added up to more than was
 * needed - returning the leftover as a brand-new change output, the same
 * way paying with a banknote larger than the price gets you change back. */
int utxo_transfer(UTXOSet *set, const char *from_owner, const char *to_owner,
                   int amount, const char *fee_owner, const char *txid) {
    int needed = amount + UTXO_FEE;
    int gathered = 0;
    int *input_indices = malloc(sizeof(int) * (size_t)set->count);
    int input_count = 0;

    for (int i = 0; i < set->count && gathered < needed; i++) {
        if (strcmp(set->items[i].owner, from_owner) == 0) {
            input_indices[input_count++] = i;
            gathered += set->items[i].amount;
        }
    }

    if (gathered < needed) {
        free(input_indices);
        return 0;
    }

    /* remove the consumed inputs highest-index-first so earlier indices
     * stay valid while we remove */
    for (int i = input_count - 1; i >= 0; i--) {
        remove_at(set, input_indices[i]);
    }
    free(input_indices);

    add_output(set, txid, 0, to_owner, amount);
    add_output(set, txid, 1, fee_owner, UTXO_FEE);
    int change = gathered - needed;
    if (change > 0) {
        add_output(set, txid, 2, from_owner, change);
    }

    return 1;
}

void utxo_print_set(const UTXOSet *set) {
    if (set->count == 0) {
        printf("UTXO set is empty - no tokens have been minted yet.\n");
        return;
    }

    printf("--------------------------- UTXO SET ---------------------------\n");
    printf("%-4s %-12s %-3s %-8s %s\n", "idx", "owner", "n", "amount", "txid");
    for (int i = 0; i < set->count; i++) {
        const UTXO *u = &set->items[i];
        printf("%-4d %-12s %-3d %-8d %.16s...\n",
               i, u->owner, u->output_index, u->amount, u->txid);
    }

    printf("-------------------------- BALANCES ----------------------------\n");
    char seen[64][20];
    int seen_count = 0;
    for (int i = 0; i < set->count; i++) {
        const char *owner = set->items[i].owner;
        int already = 0;
        for (int j = 0; j < seen_count; j++) {
            if (strcmp(seen[j], owner) == 0) { already = 1; break; }
        }
        if (already || seen_count >= 64) continue;
        strncpy(seen[seen_count], owner, sizeof(seen[seen_count]) - 1);
        seen[seen_count][sizeof(seen[seen_count]) - 1] = '\0';
        seen_count++;
        printf("  %-12s %d coins\n", owner, utxo_balance(set, owner));
    }
    printf("-------------------------------------------------------------------\n");
}
