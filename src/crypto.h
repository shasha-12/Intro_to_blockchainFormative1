#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>
#include <openssl/evp.h>

#define SIGNATURE_MAX_LEN 72
#define HASH_HEX_LEN 65

void crypto_sha256_hex(const unsigned char *data, size_t len, char out_hex[HASH_HEX_LEN]);

int crypto_load_or_generate_keys(const char *priv_path, const char *pub_path, EVP_PKEY **out_pkey);

int crypto_sign(EVP_PKEY *pkey, const unsigned char *data, size_t len,
                 unsigned char *sig_out, unsigned int *sig_len);

int crypto_verify(EVP_PKEY *pkey, const unsigned char *data, size_t len,
                   const unsigned char *sig, unsigned int sig_len);

void crypto_free_key(EVP_PKEY *pkey);

#endif
