#include <stdlib.h>
#include <string.h>
#include "nonce_registry.h"

#define INITIAL_CAPACITY 16

void nonce_registry_init(NonceRegistry *reg) {
    reg->count = 0;
    reg->capacity = INITIAL_CAPACITY;
    reg->entries = malloc(sizeof(MemberNonce) * (size_t)reg->capacity);
}

void nonce_registry_free(NonceRegistry *reg) {
    free(reg->entries);
    reg->entries = NULL;
    reg->count = 0;
    reg->capacity = 0;
}

static MemberNonce *get_or_create(NonceRegistry *reg, const char *member_id) {
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->entries[i].member_id, member_id) == 0) {
            return &reg->entries[i];
        }
    }
    if (reg->count >= reg->capacity) {
        reg->capacity *= 2;
        reg->entries = realloc(reg->entries, sizeof(MemberNonce) * (size_t)reg->capacity);
    }
    MemberNonce *e = &reg->entries[reg->count];
    strncpy(e->member_id, member_id, sizeof(e->member_id) - 1);
    e->member_id[sizeof(e->member_id) - 1] = '\0';
    e->nonce = 0;
    reg->count++;
    return e;
}

unsigned long nonce_registry_get(NonceRegistry *reg, const char *member_id) {
    return get_or_create(reg, member_id)->nonce;
}

void nonce_registry_increment(NonceRegistry *reg, const char *member_id) {
    get_or_create(reg, member_id)->nonce++;
}
