#include <stdio.h>
#include <string.h>
#include "merkle.h"

static size_t build_record_buffer(const LendingRecord *r, char *out, size_t out_size) {
    char sig_hex[SIGNATURE_MAX_LEN * 2 + 1];
    for (unsigned int i = 0; i < r->sig_len; i++) {
        sprintf(sig_hex + (i * 2), "%02x", r->signature[i]);
    }
    sig_hex[r->sig_len * 2] = '\0';

    int n = snprintf(out, out_size, "%lu|%s|%s|%s|%s|%s|%s|%ld|%ld|%d|%lu|%s",
                      r->record_id, r->book_id, r->copy_id, r->book_title,
                      r->member_id, r->member_name, r->action,
                      (long)r->timestamp, (long)r->due_date, r->fine_amount,
                      r->nonce, sig_hex);
    return n > 0 ? (size_t)n : 0;
}

void merkle_leaf_hash(const LendingRecord *r, char out_hex[HASH_HEX_LEN]) {
    char buf[512];
    size_t len = build_record_buffer(r, buf, sizeof(buf));
    crypto_sha256_hex((unsigned char *)buf, len, out_hex);
}

void merkle_compute_root(const LendingRecord *records, int count, char out_root[HASH_HEX_LEN]) {
    if (count <= 0) {
        memset(out_root, '0', 64);
        out_root[64] = '\0';
        return;
    }

    char level[MAX_RECORDS_PER_BLOCK][HASH_HEX_LEN];
    int n = count;
    for (int i = 0; i < n; i++) {
        merkle_leaf_hash(&records[i], level[i]);
    }

    while (n > 1) {
        int next_n = (n + 1) / 2;
        for (int i = 0; i < next_n; i++) {
            int left = i * 2;
            int right = (left + 1 < n) ? left + 1 : left;

            char combined[HASH_HEX_LEN * 2];
            snprintf(combined, sizeof(combined), "%s%s", level[left], level[right]);
            crypto_sha256_hex((unsigned char *)combined, strlen(combined), level[i]);
        }
        n = next_n;
    }

    strcpy(out_root, level[0]);
}
