#ifndef FINES_H
#define FINES_H

typedef struct {
    char member_id[20];
    int owed;
} FineAccount;

typedef struct {
    FineAccount *accounts;
    int count;
    int capacity;
} FineLedger;

void fine_ledger_init(FineLedger *ledger);
void fine_ledger_free(FineLedger *ledger);

/* Fines are tracked separately from both token ledgers - they're a
 * librarian-facing debt (settled however the library handles real
 * money), not a blockchain token transfer, so they don't touch the
 * UTXO set or account balances at all. */
void fine_ledger_accrue(FineLedger *ledger, const char *member_id, int amount);

/* Returns 0 if the member doesn't owe at least amount. */
int fine_ledger_settle(FineLedger *ledger, const char *member_id, int amount);

int fine_ledger_owed(const FineLedger *ledger, const char *member_id);

void fine_ledger_print(const FineLedger *ledger);

#endif
