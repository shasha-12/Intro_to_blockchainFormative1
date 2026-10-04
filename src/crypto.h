#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>
#include <time.h>
#include <openssl/evp.h>

#define SIGNATURE_MAX_LEN 72
#define HASH_HEX_LEN 65

void crypto_sha256_hex(const unsigned char *data, size_t len, char out_hex[HASH_HEX_LEN]);

/* A deterministic transaction id: SHA-256 of who paid whom, how much, and
 * when. Deterministic (no internal clock read) so the same id can be
 * computed once at transaction-creation time and reused later, rather
 * than drifting if recomputed after time has passed. */
void crypto_compute_transaction_id(const char *from, const char *to, int amount,
                                    time_t ts, char out_txid[HASH_HEX_LEN]);

int crypto_load_or_generate_keys(const char *priv_path, const char *pub_path, EVP_PKEY **out_pkey);

int crypto_sign(EVP_PKEY *pkey, const unsigned char *data, size_t len,
                 unsigned char *sig_out, unsigned int *sig_len);

int crypto_verify(EVP_PKEY *pkey, const unsigned char *data, size_t len,
                   const unsigned char *sig, unsigned int sig_len);

void crypto_free_key(EVP_PKEY *pkey);

#endif
