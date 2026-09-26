#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <time.h>
#include <openssl/evp.h>
#include "crypto.h"

#define ACTION_BORROWED "BORROWED"
#define ACTION_RETURNED "RETURNED"
#define ACTION_OVERDUE  "OVERDUE"
#define ACTION_GENESIS  "GENESIS"

typedef struct {
    int index;
    time_t timestamp;
    char book_id[20];
    char book_title[80];
    char member_id[20];
    char member_name[50];
    char action[10];
    char previous_hash[65];
    unsigned char signature[SIGNATURE_MAX_LEN];
    unsigned int sig_len;
    char hash[65];
} Block;

typedef struct {
    Block *blocks;
    int count;
    int capacity;
} Blockchain;

void blockchain_init(Blockchain *chain);
void blockchain_free(Blockchain *chain);

int blockchain_add_genesis(Blockchain *chain, EVP_PKEY *pkey);

int blockchain_add_block(Blockchain *chain, EVP_PKEY *pkey,
                          const char *book_id, const char *book_title,
                          const char *member_id, const char *member_name,
                          const char *action);

const Block *blockchain_last_action_for_book(const Blockchain *chain, const char *book_id);

void blockchain_recompute_hash(const Block *b, char out_hex[HASH_HEX_LEN]);

int blockchain_verify_signature(const Block *b, EVP_PKEY *pkey);

int blockchain_validate(const Blockchain *chain, EVP_PKEY *pkey);

void blockchain_print_records(const Blockchain *chain, EVP_PKEY *pkey);

#endif
