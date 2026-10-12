#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fines.h"

#define INITIAL_CAPACITY 16

void fine_ledger_init(FineLedger *ledger) {
    ledger->count = 0;
    ledger->capacity = INITIAL_CAPACITY;
    ledger->accounts = malloc(sizeof(FineAccount) * (size_t)ledger->capacity);
}

void fine_ledger_free(FineLedger *ledger) {
    free(ledger->accounts);
    ledger->accounts = NULL;
    ledger->count = 0;
    ledger->capacity = 0;
}

static FineAccount *get_or_create(FineLedger *ledger, const char *member_id) {
    for (int i = 0; i < ledger->count; i++) {
        if (strcmp(ledger->accounts[i].member_id, member_id) == 0) {
            return &ledger->accounts[i];
        }
    }
    if (ledger->count >= ledger->capacity) {
        ledger->capacity *= 2;
        ledger->accounts = realloc(ledger->accounts, sizeof(FineAccount) * (size_t)ledger->capacity);
    }
    FineAccount *a = &ledger->accounts[ledger->count];
    strncpy(a->member_id, member_id, sizeof(a->member_id) - 1);
    a->member_id[sizeof(a->member_id) - 1] = '\0';
    a->owed = 0;
    ledger->count++;
    return a;
}

void fine_ledger_accrue(FineLedger *ledger, const char *member_id, int amount) {
    if (amount <= 0) return;
    get_or_create(ledger, member_id)->owed += amount;
}

int fine_ledger_settle(FineLedger *ledger, const char *member_id, int amount) {
    if (amount <= 0) return 0;
    FineAccount *a = get_or_create(ledger, member_id);
    if (a->owed < amount) return 0;
    a->owed -= amount;
    return 1;
}

int fine_ledger_owed(const FineLedger *ledger, const char *member_id) {
    for (int i = 0; i < ledger->count; i++) {
        if (strcmp(ledger->accounts[i].member_id, member_id) == 0) {
            return ledger->accounts[i].owed;
        }
    }
    return 0;
}

void fine_ledger_print(const FineLedger *ledger) {
    int any = 0;
    printf("--------------------------- FINES OWED ---------------------------\n");
    for (int i = 0; i < ledger->count; i++) {
        if (ledger->accounts[i].owed > 0) {
            printf("  %-14s %d\n", ledger->accounts[i].member_id, ledger->accounts[i].owed);
            any = 1;
        }
    }
    if (!any) {
        printf("  No outstanding fines.\n");
    }
    printf("---------------------------------------------------------------------\n");
}
