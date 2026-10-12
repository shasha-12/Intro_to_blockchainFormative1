#ifndef NONCE_REGISTRY_H
#define NONCE_REGISTRY_H

/* Tracks each member's CONFIRMED lending-request nonce - distinct from
 * the account model's token-transfer nonce in account.h, which is a
 * separate counter for a separate kind of action. This one only ever
 * advances when a borrow/return/renew request is actually mined, per
 * the brief's "the nonce increments only upon confirmation" rule - a
 * request sitting signed in the pending pool does not change it. */
typedef struct {
    char member_id[20];
    unsigned long nonce;
} MemberNonce;

typedef struct {
    MemberNonce *entries;
    int count;
    int capacity;
} NonceRegistry;

void nonce_registry_init(NonceRegistry *reg);
void nonce_registry_free(NonceRegistry *reg);

/* Current confirmed nonce for member_id (0 if never seen before). */
unsigned long nonce_registry_get(NonceRegistry *reg, const char *member_id);

void nonce_registry_increment(NonceRegistry *reg, const char *member_id);

#endif
