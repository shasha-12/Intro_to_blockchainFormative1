#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "account.h"

#define INITIAL_CAPACITY 16

void account_ledger_init(AccountLedger *ledger) {
    ledger->count = 0;
    ledger->capacity = INITIAL_CAPACITY;
    ledger->accounts = malloc(sizeof(Account) * (size_t)ledger->capacity);
}

/* Each account's history is its own singly-linked list, so freeing the
 * ledger means walking every account's list node by node before freeing
 * the accounts array itself - otherwise every TxRecord malloc'd in
 * prepend_history would leak. */
void account_ledger_free(AccountLedger *ledger) {
    for (int i = 0; i < ledger->count; i++) {
        TxRecord *node = ledger->accounts[i].history;
        while (node != NULL) {
            TxRecord *next = node->next;
            free(node);
            node = next;
        }
    }
    free(ledger->accounts);
    ledger->accounts = NULL;
    ledger->count = 0;
    ledger->capacity = 0;
}

static void ensure_capacity(AccountLedger *ledger) {
    if (ledger->count >= ledger->capacity) {
        ledger->capacity *= 2;
        ledger->accounts = realloc(ledger->accounts, sizeof(Account) * (size_t)ledger->capacity);
    }
}

Account *account_get_or_create(AccountLedger *ledger, const char *member_id) {
    for (int i = 0; i < ledger->count; i++) {
        if (strcmp(ledger->accounts[i].member_id, member_id) == 0) {
            return &ledger->accounts[i];
        }
    }

    ensure_capacity(ledger);
    Account *a = &ledger->accounts[ledger->count];
    strncpy(a->member_id, member_id, sizeof(a->member_id) - 1);
    a->member_id[sizeof(a->member_id) - 1] = '\0';
    a->balance = 0;
    a->nonce = 0;
    a->history = NULL;
    ledger->count++;
    return a;
}

int account_balance(AccountLedger *ledger, const char *member_id) {
    return account_get_or_create(ledger, member_id)->balance;
}

/* Prepends rather than appends so the history always reads most-recent-first
 * without needing to walk to the tail or track one separately. */
static void prepend_history(Account *a, const char *sender, const char *recipient,
                             int amount, int fee, unsigned long nonce) {
    TxRecord *node = malloc(sizeof(TxRecord));
    strncpy(node->sender, sender, sizeof(node->sender) - 1);
    node->sender[sizeof(node->sender) - 1] = '\0';
    strncpy(node->recipient, recipient, sizeof(node->recipient) - 1);
    node->recipient[sizeof(node->recipient) - 1] = '\0';
    node->amount = amount;
    node->fee = fee;
    node->nonce = nonce;
    node->next = a->history;
    a->history = node;
}

/* Used only for miner block rewards - unlike account_credit_reward, there's
 * no fee here, because a mining reward isn't a transaction between two
 * members, it's newly-issued currency with no sender to deduct a fee from. */
void account_credit_direct(AccountLedger *ledger, const char *member_id, int amount) {
    if (amount <= 0) return;
    Account *a = account_get_or_create(ledger, member_id);
    a->balance += amount;
    prepend_history(a, "MINING_REWARD", member_id, amount, 0, 0);
}

/* Pays out a confirmed RETURNED block's token_reward. A flat ACCOUNT_FEE is
 * taken off the top and routed to fee_owner (the same flat-fee idea as the
 * UTXO model, kept consistent across both so they're comparable). If the
 * reward can't even cover its own fee, nothing is paid - this can't happen
 * with the current 10/5 reward tiers, but the check keeps the function
 * correct if those constants ever change. */
int account_credit_reward(AccountLedger *ledger, const char *member_id, int amount,
                           const char *fee_owner) {
    if (amount <= ACCOUNT_FEE) {
        return 0;
    }

    Account *recipient = account_get_or_create(ledger, member_id);
    Account *fee_account = account_get_or_create(ledger, fee_owner);

    recipient->balance += (amount - ACCOUNT_FEE);
    fee_account->balance += ACCOUNT_FEE;

    prepend_history(recipient, "SYSTEM", member_id, amount - ACCOUNT_FEE, ACCOUNT_FEE, 0);
    return 1;
}

/* A manual, member-to-member transfer. The nonce check is what stops the
 * exact same transfer instruction being replayed twice: a submitted nonce
 * only succeeds if it matches the sender's current nonce exactly, and the
 * sender's nonce only advances on a successful transfer - so once a nonce
 * has been consumed, resubmitting it (even with a perfectly valid balance)
 * is rejected outright. The balance check separately covers amount + fee,
 * so a sender can't drain their account below what the fee requires. */
AccountResult account_transfer(AccountLedger *ledger, const char *sender_id,
                                const char *recipient_id, int amount,
                                unsigned long submitted_nonce, const char *fee_owner) {
    Account *sender = account_get_or_create(ledger, sender_id);

    if (submitted_nonce != sender->nonce) {
        return ACCOUNT_ERR_BAD_NONCE;
    }

    if (sender->balance < amount + ACCOUNT_FEE) {
        return ACCOUNT_ERR_INSUFFICIENT_BALANCE;
    }

    Account *recipient = account_get_or_create(ledger, recipient_id);
    Account *fee_account = account_get_or_create(ledger, fee_owner);

    sender->balance -= (amount + ACCOUNT_FEE);
    recipient->balance += amount;
    fee_account->balance += ACCOUNT_FEE;
    sender->nonce++;

    prepend_history(sender, sender_id, recipient_id, amount, ACCOUNT_FEE, submitted_nonce);
    if (strcmp(sender_id, recipient_id) != 0) {
        prepend_history(recipient, sender_id, recipient_id, amount, ACCOUNT_FEE, submitted_nonce);
    }

    return ACCOUNT_OK;
}

void account_print_all(const AccountLedger *ledger) {
    if (ledger->count == 0) {
        printf("No accounts yet.\n");
        return;
    }

    printf("--------------------- ACCOUNT BALANCES ---------------------\n");
    printf("%-14s %-10s %s\n", "member", "balance", "next nonce");
    for (int i = 0; i < ledger->count; i++) {
        const Account *a = &ledger->accounts[i];
        printf("%-14s %-10d %lu\n", a->member_id, a->balance, a->nonce);
    }
    printf("---------------------------------------------------------------\n");
}

void account_print_history(const AccountLedger *ledger, const char *member_id) {
    const Account *found = NULL;
    for (int i = 0; i < ledger->count; i++) {
        if (strcmp(ledger->accounts[i].member_id, member_id) == 0) {
            found = &ledger->accounts[i];
            break;
        }
    }

    if (found == NULL) {
        printf("No account found for '%s'.\n", member_id);
        return;
    }

    if (found->history == NULL) {
        printf("No transaction history for '%s' yet.\n", member_id);
        return;
    }

    printf("------------- TRANSACTION HISTORY: %s (most recent first) -------------\n", member_id);
    printf("%-14s %-14s %-8s %-6s %s\n", "sender", "recipient", "amount", "fee", "nonce");
    for (const TxRecord *node = found->history; node != NULL; node = node->next) {
        printf("%-14s %-14s %-8d %-6d %lu\n",
               node->sender, node->recipient, node->amount, node->fee, node->nonce);
    }
    printf("---------------------------------------------------------------------------\n");
}
