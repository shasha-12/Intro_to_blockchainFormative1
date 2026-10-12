#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "member_keys.h"
#include "crypto.h"

#define INITIAL_CAPACITY 16
#define KEY_DIR "keys/members"

void member_key_store_init(MemberKeyStore *store) {
    store->count = 0;
    store->capacity = INITIAL_CAPACITY;
    store->entries = malloc(sizeof(MemberKeyEntry) * (size_t)store->capacity);
    mkdir("keys", 0755);
    mkdir(KEY_DIR, 0755);
}

void member_key_store_free(MemberKeyStore *store) {
    for (int i = 0; i < store->count; i++) {
        crypto_free_key(store->entries[i].pkey);
    }
    free(store->entries);
    store->entries = NULL;
    store->count = 0;
    store->capacity = 0;
}

EVP_PKEY *member_key_store_get(MemberKeyStore *store, const char *member_id) {
    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->entries[i].member_id, member_id) == 0) {
            return store->entries[i].pkey;
        }
    }

    char priv_path[80], pub_path[80];
    snprintf(priv_path, sizeof(priv_path), "%s/%s_private.pem", KEY_DIR, member_id);
    snprintf(pub_path, sizeof(pub_path), "%s/%s_public.pem", KEY_DIR, member_id);

    EVP_PKEY *pkey = NULL;
    if (!crypto_load_or_generate_keys(priv_path, pub_path, &pkey)) {
        return NULL;
    }

    if (store->count >= store->capacity) {
        store->capacity *= 2;
        store->entries = realloc(store->entries, sizeof(MemberKeyEntry) * (size_t)store->capacity);
    }
    MemberKeyEntry *e = &store->entries[store->count];
    strncpy(e->member_id, member_id, sizeof(e->member_id) - 1);
    e->member_id[sizeof(e->member_id) - 1] = '\0';
    e->pkey = pkey;
    store->count++;
    return pkey;
}
