#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blockchain.h"

#define INITIAL_CAPACITY 16
#define SIGNABLE_BUF_SIZE 512
#define FULL_BUF_SIZE 768

void blockchain_init(Blockchain *chain) {
    chain->count = 0;
    chain->capacity = INITIAL_CAPACITY;
    chain->blocks = malloc(sizeof(Block) * (size_t)chain->capacity);
}

void blockchain_free(Blockchain *chain) {
    free(chain->blocks);
    chain->blocks = NULL;
    chain->count = 0;
    chain->capacity = 0;
}

static void ensure_capacity(Blockchain *chain) {
    if (chain->count >= chain->capacity) {
        chain->capacity *= 2;
        chain->blocks = realloc(chain->blocks, sizeof(Block) * (size_t)chain->capacity);
    }
}

static void hex_encode(const unsigned char *data, unsigned int len, char *out) {
    for (unsigned int i = 0; i < len; i++) {
        sprintf(out + (i * 2), "%02x", data[i]);
    }
    out[len * 2] = '\0';
}

static size_t build_signable_buffer(const Block *b, char *out, size_t out_size) {
    int n = snprintf(out, out_size, "%d|%ld|%s|%s|%s|%s|%s|%s",
                      b->index, (long)b->timestamp, b->book_id, b->book_title,
                      b->member_id, b->member_name, b->action, b->previous_hash);
    return n > 0 ? (size_t)n : 0;
}

static size_t build_full_buffer(const Block *b, char *out, size_t out_size) {
    char sig_hex[SIGNATURE_MAX_LEN * 2 + 1];
    hex_encode(b->signature, b->sig_len, sig_hex);

    int n = snprintf(out, out_size, "%d|%ld|%s|%s|%s|%s|%s|%s|%s",
                      b->index, (long)b->timestamp, b->book_id, b->book_title,
                      b->member_id, b->member_name, b->action, b->previous_hash, sig_hex);
    return n > 0 ? (size_t)n : 0;
}

void blockchain_recompute_hash(const Block *b, char out_hex[HASH_HEX_LEN]) {
    char buf[FULL_BUF_SIZE];
    size_t len = build_full_buffer(b, buf, sizeof(buf));
    crypto_sha256_hex((unsigned char *)buf, len, out_hex);
}

int blockchain_verify_signature(const Block *b, EVP_PKEY *pkey) {
    char buf[SIGNABLE_BUF_SIZE];
    size_t len = build_signable_buffer(b, buf, sizeof(buf));
    return crypto_verify(pkey, (unsigned char *)buf, len, b->signature, b->sig_len);
}

static int sign_and_finalize(Block *b, EVP_PKEY *pkey) {
    char signable[SIGNABLE_BUF_SIZE];
    size_t slen = build_signable_buffer(b, signable, sizeof(signable));

    if (!crypto_sign(pkey, (unsigned char *)signable, slen, b->signature, &b->sig_len)) {
        return 0;
    }

    blockchain_recompute_hash(b, b->hash);
    return 1;
}

int blockchain_add_genesis(Blockchain *chain, EVP_PKEY *pkey) {
    ensure_capacity(chain);
    Block *b = &chain->blocks[chain->count];

    b->index = 0;
    b->timestamp = time(NULL);
    strcpy(b->book_id, "");
    strcpy(b->book_title, "");
    strcpy(b->member_id, "");
    strcpy(b->member_name, "");
    strncpy(b->action, ACTION_GENESIS, sizeof(b->action) - 1);
    b->action[sizeof(b->action) - 1] = '\0';
    memset(b->previous_hash, '0', 64);
    b->previous_hash[64] = '\0';

    if (!sign_and_finalize(b, pkey)) return 0;
    chain->count++;
    return 1;
}

int blockchain_add_block(Blockchain *chain, EVP_PKEY *pkey,
                          const char *book_id, const char *book_title,
                          const char *member_id, const char *member_name,
                          const char *action) {
    if (chain->count == 0) {
        fprintf(stderr, "ERROR: Cannot add a block before the genesis block exists\n");
        return 0;
    }

    ensure_capacity(chain);
    Block *b = &chain->blocks[chain->count];
    const Block *prev = &chain->blocks[chain->count - 1];

    b->index = prev->index + 1;
    b->timestamp = time(NULL);
    strncpy(b->book_id, book_id, sizeof(b->book_id) - 1);
    b->book_id[sizeof(b->book_id) - 1] = '\0';
    strncpy(b->book_title, book_title, sizeof(b->book_title) - 1);
    b->book_title[sizeof(b->book_title) - 1] = '\0';
    strncpy(b->member_id, member_id, sizeof(b->member_id) - 1);
    b->member_id[sizeof(b->member_id) - 1] = '\0';
    strncpy(b->member_name, member_name, sizeof(b->member_name) - 1);
    b->member_name[sizeof(b->member_name) - 1] = '\0';
    strncpy(b->action, action, sizeof(b->action) - 1);
    b->action[sizeof(b->action) - 1] = '\0';
    strncpy(b->previous_hash, prev->hash, sizeof(b->previous_hash) - 1);
    b->previous_hash[sizeof(b->previous_hash) - 1] = '\0';

    if (!sign_and_finalize(b, pkey)) return 0;
    chain->count++;
    return 1;
}

const Block *blockchain_last_action_for_book(const Blockchain *chain, const char *book_id) {
    for (int i = chain->count - 1; i >= 1; i--) {
        if (strcmp(chain->blocks[i].book_id, book_id) == 0) {
            return &chain->blocks[i];
        }
    }
    return NULL;
}

int blockchain_validate(const Blockchain *chain, EVP_PKEY *pkey) {
    int all_valid = 1;

    for (int i = 0; i < chain->count; i++) {
        const Block *b = &chain->blocks[i];

        char recomputed[HASH_HEX_LEN];
        blockchain_recompute_hash(b, recomputed);
        int hash_ok = strcmp(recomputed, b->hash) == 0;

        int link_ok = 1;
        if (i > 0) {
            link_ok = strcmp(b->previous_hash, chain->blocks[i - 1].hash) == 0;
        } else {
            char zeros[65];
            memset(zeros, '0', 64);
            zeros[64] = '\0';
            link_ok = strcmp(b->previous_hash, zeros) == 0;
        }

        int sig_ok = blockchain_verify_signature(b, pkey);

        printf("Block %d [%s]: hash %s, link %s, signature %s\n",
               b->index, b->action,
               hash_ok ? "OK" : "MISMATCH",
               link_ok ? "OK" : "BROKEN",
               sig_ok ? "VALID" : "INVALID");

        if (!hash_ok || !link_ok || !sig_ok) {
            all_valid = 0;
        }
    }

    return all_valid;
}

static const char *format_timestamp(time_t ts, char *out, size_t out_size) {
    struct tm *tm_info = localtime(&ts);
    strftime(out, out_size, "%Y-%m-%d %H:%M:%S", tm_info);
    return out;
}

void blockchain_print_records(const Blockchain *chain, EVP_PKEY *pkey) {
    for (int i = 0; i < chain->count; i++) {
        const Block *b = &chain->blocks[i];
        char ts[32];
        format_timestamp(b->timestamp, ts, sizeof(ts));
        int sig_ok = blockchain_verify_signature(b, pkey);

        printf("---------------------------------------------\n");
        printf("Block #%d\n", b->index);
        printf("  Timestamp     : %s\n", ts);
        if (b->index == 0) {
            printf("  Action        : %s\n", b->action);
        } else {
            printf("  Book          : %s (%s)\n", b->book_title, b->book_id);
            printf("  Member        : %s (%s)\n", b->member_name, b->member_id);
            printf("  Action        : %s\n", b->action);
        }
        printf("  Previous Hash : %s\n", b->previous_hash);
        printf("  Hash          : %s\n", b->hash);
        printf("  Signature     : %s\n", sig_ok ? "VALID" : "INVALID");
    }
    printf("---------------------------------------------\n");
}
