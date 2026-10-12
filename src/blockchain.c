#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blockchain.h"
#include "merkle.h"

#define INITIAL_CAPACITY 16
#define RECORD_SIGNABLE_BUF 384
#define BLOCK_BUF_SIZE 384

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

static size_t build_record_signable(const LendingRecord *r, char *out, size_t out_size) {
    int n = snprintf(out, out_size, "%lu|%s|%s|%s|%s|%s|%s|%ld|%ld|%d|%lu",
                      r->record_id, r->book_id, r->copy_id, r->book_title,
                      r->member_id, r->member_name, r->action,
                      (long)r->timestamp, (long)r->due_date, r->fine_amount, r->nonce);
    return n > 0 ? (size_t)n : 0;
}

int blockchain_sign_record(LendingRecord *r, EVP_PKEY *pkey) {
    char buf[RECORD_SIGNABLE_BUF];
    size_t len = build_record_signable(r, buf, sizeof(buf));
    return crypto_sign(pkey, (unsigned char *)buf, len, r->signature, &r->sig_len);
}

int blockchain_verify_record(const LendingRecord *r, EVP_PKEY *pkey) {
    char buf[RECORD_SIGNABLE_BUF];
    size_t len = build_record_signable(r, buf, sizeof(buf));
    return crypto_verify(pkey, (unsigned char *)buf, len, r->signature, r->sig_len);
}

static size_t build_block_buffer(const Block *b, char *out, size_t out_size) {
    int n = snprintf(out, out_size, "%d|%ld|%d|%s|%s|%s|%d|%s|%lu",
                      b->index, (long)b->timestamp, b->record_count,
                      b->previous_hash, b->merkle_root, b->sealed_by,
                      b->token_reward, b->transaction_id, b->nonce);
    return n > 0 ? (size_t)n : 0;
}

void blockchain_recompute_hash(const Block *b, char out_hex[HASH_HEX_LEN]) {
    char buf[BLOCK_BUF_SIZE];
    size_t len = build_block_buffer(b, buf, sizeof(buf));
    crypto_sha256_hex((unsigned char *)buf, len, out_hex);
}

void blockchain_append_confirmed(Blockchain *chain, const Block *b) {
    ensure_capacity(chain);
    chain->blocks[chain->count] = *b;
    chain->count++;
}

int blockchain_add_genesis(Blockchain *chain) {
    ensure_capacity(chain);
    Block *b = &chain->blocks[chain->count];

    b->index = 0;
    b->timestamp = time(NULL);
    b->record_count = 0;
    memset(b->previous_hash, '0', 64);
    b->previous_hash[64] = '\0';
    merkle_compute_root(NULL, 0, b->merkle_root);
    strncpy(b->sealed_by, ACTION_GENESIS, sizeof(b->sealed_by) - 1);
    b->sealed_by[sizeof(b->sealed_by) - 1] = '\0';
    b->nonce = 0;
    b->token_reward = 0;
    memset(b->transaction_id, '0', 64);
    b->transaction_id[64] = '\0';

    blockchain_recompute_hash(b, b->hash);
    chain->count++;
    return 1;
}

void blockchain_seal_block(Block *b, const LendingRecord *records, int count,
                            int index, const char *previous_hash, const char *sealed_by) {
    b->index = index;
    b->timestamp = time(NULL);
    b->record_count = count;
    for (int i = 0; i < count; i++) {
        b->records[i] = records[i];
    }
    strncpy(b->previous_hash, previous_hash, sizeof(b->previous_hash) - 1);
    b->previous_hash[sizeof(b->previous_hash) - 1] = '\0';
    merkle_compute_root(records, count, b->merkle_root);
    strncpy(b->sealed_by, sealed_by, sizeof(b->sealed_by) - 1);
    b->sealed_by[sizeof(b->sealed_by) - 1] = '\0';
    b->nonce = 0;
    b->token_reward = 0;
    memset(b->transaction_id, '0', 64);
    b->transaction_id[64] = '\0';
    memset(b->hash, '0', 64);
    b->hash[64] = '\0';
}

const LendingRecord *blockchain_last_action_for_copy(const Blockchain *chain, const char *copy_id) {
    for (int bi = chain->count - 1; bi >= 1; bi--) {
        const Block *b = &chain->blocks[bi];
        for (int ri = b->record_count - 1; ri >= 0; ri--) {
            if (strcmp(b->records[ri].copy_id, copy_id) == 0) {
                return &b->records[ri];
            }
        }
    }
    return NULL;
}

int blockchain_validate(const Blockchain *chain, EVP_PKEY *pkey) {
    int all_valid = 1;

    for (int i = 0; i < chain->count; i++) {
        const Block *b = &chain->blocks[i];

        char recomputed_hash[HASH_HEX_LEN];
        blockchain_recompute_hash(b, recomputed_hash);
        int hash_ok = strcmp(recomputed_hash, b->hash) == 0;

        char recomputed_root[HASH_HEX_LEN];
        merkle_compute_root(b->records, b->record_count, recomputed_root);
        int merkle_ok = strcmp(recomputed_root, b->merkle_root) == 0;

        int link_ok = 1;
        if (i > 0) {
            link_ok = strcmp(b->previous_hash, chain->blocks[i - 1].hash) == 0;
        } else {
            char zeros[65];
            memset(zeros, '0', 64);
            zeros[64] = '\0';
            link_ok = strcmp(b->previous_hash, zeros) == 0;
        }

        int sig_ok = 1;
        for (int r = 0; r < b->record_count; r++) {
            if (!blockchain_verify_record(&b->records[r], pkey)) {
                sig_ok = 0;
                break;
            }
        }

        printf("Block %d [%d record(s)]: hash %s, link %s, merkle %s, signatures %s\n",
               b->index, b->record_count,
               hash_ok ? "OK" : "MISMATCH",
               link_ok ? "OK" : "BROKEN",
               merkle_ok ? "OK" : "MISMATCH",
               sig_ok ? "VALID" : "INVALID");

        if (!hash_ok || !link_ok || !merkle_ok || !sig_ok) {
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

        printf("---------------------------------------------\n");
        printf("Block #%d\n", b->index);
        printf("  Timestamp     : %s\n", ts);
        printf("  Sealed By     : %s\n", b->sealed_by);
        printf("  Record Count  : %d\n", b->record_count);
        printf("  Token Reward  : %d\n", b->token_reward);
        printf("  Merkle Root   : %.16s...\n", b->merkle_root);
        printf("  Previous Hash : %s\n", b->previous_hash);
        printf("  Hash          : %s\n", b->hash);

        for (int r = 0; r < b->record_count; r++) {
            const LendingRecord *rec = &b->records[r];
            int sig_ok = blockchain_verify_record(rec, pkey);
            char rts[32];
            format_timestamp(rec->timestamp, rts, sizeof(rts));
            printf("  --- Record %d ---\n", r);
            printf("    Book    : %s (%s / copy %s)\n", rec->book_title, rec->book_id, rec->copy_id);
            printf("    Member  : %s (%s)\n", rec->member_name, rec->member_id);
            printf("    Action  : %s at %s\n", rec->action, rts);
            if (rec->fine_amount > 0) {
                printf("    Fine    : %d\n", rec->fine_amount);
            }
            printf("    Signature : %s\n", sig_ok ? "VALID" : "INVALID");
        }
    }
    printf("---------------------------------------------\n");
}
