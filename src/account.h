#ifndef ACCOUNT_H
#define ACCOUNT_H

#define ACCOUNT_FEE 1

typedef struct TxRecord {
    char sender[20];
    char recipient[20];
    int amount;
    int fee;
    unsigned long nonce;
    struct TxRecord *next;
} TxRecord;

typedef struct {
    char member_id[20];
    int balance;
    unsigned long nonce; /* the nonce this account expects on its NEXT outgoing transfer */
    TxRecord *history;   /* linked list, most recent first */
} Account;

typedef struct {
    Account *accounts;
    int count;
    int capacity;
} AccountLedger;

typedef enum {
    ACCOUNT_OK = 0,
    ACCOUNT_ERR_BAD_NONCE,
    ACCOUNT_ERR_INSUFFICIENT_BALANCE,
} AccountResult;

void account_ledger_init(AccountLedger *ledger);
void account_ledger_free(AccountLedger *ledger);

/* Finds member_id's account, creating one (balance 0, nonce 0) if it
 * doesn't exist yet. Never returns NULL. */
Account *account_get_or_create(AccountLedger *ledger, const char *member_id);

int account_balance(AccountLedger *ledger, const char *member_id);

/* Credits a flat amount directly, no fee, no nonce involvement - used
 * for a miner's block reward, which isn't a transaction "between" two
 * parties. Still logged to the account's history with sender
 * "MINING_REWARD" so it's traceable. */
void account_credit_direct(AccountLedger *ledger, const char *member_id, int amount);

/* Credits a reward straight to member_id's balance, minus the fixed fee
 * (which goes to fee_owner's balance). This is a system credit, not an
 * outgoing transaction, so it does not touch member_id's nonce. Returns
 * 1 on success, 0 if amount doesn't exceed the fee. */
int account_credit_reward(AccountLedger *ledger, const char *member_id, int amount,
                           const char *fee_owner);

/* Attempts a manual transfer. Rejects with ACCOUNT_ERR_BAD_NONCE if
 * submitted_nonce isn't exactly the sender's current expected nonce
 * (this is what stops a transaction being replayed/resubmitted), or
 * with ACCOUNT_ERR_INSUFFICIENT_BALANCE if the sender's balance can't
 * cover amount + fee. On success: debits the sender, credits the
 * recipient and fee_owner, increments the sender's nonce, and appends a
 * history record to both the sender's and recipient's account. */
AccountResult account_transfer(AccountLedger *ledger, const char *sender_id,
                                const char *recipient_id, int amount,
                                unsigned long submitted_nonce, const char *fee_owner);

void account_print_all(const AccountLedger *ledger);
void account_print_history(const AccountLedger *ledger, const char *member_id);

#endif
