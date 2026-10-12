#ifndef MEMBER_KEYS_H
#define MEMBER_KEYS_H

#include <openssl/evp.h>

/* Formative 1/2 signed everything with one shared librarian keypair.
 * The summative requires every member to have their own identity, so
 * this replaces that with a lazily-populated per-member keypair cache -
 * each member's private key is generated the first time they're looked
 * up and reused (from keys/members/<id>_private.pem) on every later
 * lookup or program run, exactly like the old single keypair was. */
typedef struct {
    char member_id[20];
    EVP_PKEY *pkey;
} MemberKeyEntry;

typedef struct {
    MemberKeyEntry *entries;
    int count;
    int capacity;
} MemberKeyStore;

void member_key_store_init(MemberKeyStore *store);

/* Frees every loaded EVP_PKEY along with the store itself. */
void member_key_store_free(MemberKeyStore *store);

/* Returns member_id's keypair, generating and persisting a new one on
 * first use. Returns NULL only if key generation itself fails. */
EVP_PKEY *member_key_store_get(MemberKeyStore *store, const char *member_id);

#endif
