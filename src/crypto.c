#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include "crypto.h"

void crypto_sha256_hex(const unsigned char *data, size_t len, char out_hex[HASH_HEX_LEN]) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(data, len, digest);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(out_hex + (i * 2), "%02x", digest[i]);
    }
    out_hex[HASH_HEX_LEN - 1] = '\0';
}

/* Deterministic on purpose: given the same inputs this always produces the
 * same txid, with no internal clock read. That matters because a reward's
 * txid is computed once when a RETURNED block is created (while it's still
 * only pending), then reused unchanged when the reward is actually paid out
 * at mining time - the on-chain record and the ledger entry must agree on
 * one transaction ID, not two different ones generated at two different
 * moments. */
void crypto_compute_transaction_id(const char *from, const char *to, int amount,
                                    time_t ts, char out_txid[HASH_HEX_LEN]) {
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "%s|%s|%d|%ld", from, to, amount, (long)ts);
    crypto_sha256_hex((unsigned char *)buf, (size_t)n, out_txid);
}

static EVP_PKEY *generate_keypair(void) {
    EVP_PKEY *pkey = EVP_EC_gen("P-256");
    if (pkey == NULL) {
        fprintf(stderr, "ERROR: Failed to generate ECDSA keypair\n");
        ERR_print_errors_fp(stderr);
    }
    return pkey;
}

static int save_keypair(EVP_PKEY *pkey, const char *priv_path, const char *pub_path) {
    FILE *fp = fopen(priv_path, "w");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: Could not write private key to '%s'\n", priv_path);
        return 0;
    }
    int ok = PEM_write_PrivateKey(fp, pkey, NULL, NULL, 0, NULL, NULL);
    fclose(fp);
    if (!ok) {
        fprintf(stderr, "ERROR: Failed to serialize private key\n");
        return 0;
    }

    fp = fopen(pub_path, "w");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: Could not write public key to '%s'\n", pub_path);
        return 0;
    }
    ok = PEM_write_PUBKEY(fp, pkey);
    fclose(fp);
    if (!ok) {
        fprintf(stderr, "ERROR: Failed to serialize public key\n");
        return 0;
    }
    return 1;
}

static EVP_PKEY *load_private_key(const char *priv_path) {
    FILE *fp = fopen(priv_path, "r");
    if (fp == NULL) return NULL;
    EVP_PKEY *pkey = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
    fclose(fp);
    return pkey;
}

/* Reuses the same keypair across runs so a block signed in one session
 * still verifies in the next - generating a fresh key every launch would
 * make every previously-signed block fail verification the moment the
 * program restarted. */
int crypto_load_or_generate_keys(const char *priv_path, const char *pub_path, EVP_PKEY **out_pkey) {
    EVP_PKEY *pkey = load_private_key(priv_path);
    if (pkey != NULL) {
        *out_pkey = pkey;
        return 1;
    }

    printf("No existing signing key found — generating a new ECDSA (P-256) keypair...\n");
    pkey = generate_keypair();
    if (pkey == NULL) return 0;

    if (!save_keypair(pkey, priv_path, pub_path)) {
        EVP_PKEY_free(pkey);
        return 0;
    }

    printf("Keypair saved to '%s' and '%s'\n", priv_path, pub_path);
    *out_pkey = pkey;
    return 1;
}

int crypto_sign(EVP_PKEY *pkey, const unsigned char *data, size_t len,
                 unsigned char *sig_out, unsigned int *sig_len) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) return 0;

    size_t out_len = SIGNATURE_MAX_LEN;
    int ok = EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, pkey) == 1 &&
             EVP_DigestSign(ctx, sig_out, &out_len, data, len) == 1;

    EVP_MD_CTX_free(ctx);
    if (!ok) {
        fprintf(stderr, "ERROR: Failed to sign block data\n");
        ERR_print_errors_fp(stderr);
        return 0;
    }
    *sig_len = (unsigned int)out_len;
    return 1;
}

int crypto_verify(EVP_PKEY *pkey, const unsigned char *data, size_t len,
                   const unsigned char *sig, unsigned int sig_len) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) return 0;

    int ok = EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey) == 1 &&
              EVP_DigestVerify(ctx, sig, sig_len, data, len) == 1;

    EVP_MD_CTX_free(ctx);
    return ok ? 1 : 0;
}

void crypto_free_key(EVP_PKEY *pkey) {
    EVP_PKEY_free(pkey);
}
