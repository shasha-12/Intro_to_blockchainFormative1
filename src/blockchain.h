#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <time.h>
#include <openssl/evp.h>
#include "crypto.h"
#include "member_keys.h"

#define ACTION_BORROWED "BORROWED"
#define ACTION_RETURNED "RETURNED"
#define ACTION_RESERVED "RESERVED"
#define ACTION_RENEWED  "RENEWED"
#define ACTION_OVERDUE  "OVERDUE"
#define ACTION_GENESIS  "GENESIS"

#define MAX_RECORDS_PER_BLOCK 8
#define SEALED_BY_MAX 50

/* One lending action, signed individually by whoever requested it
 * (a member or the librarian). A block batches several of these -
 * see merkle.h for how they're combined into one merkle_root. */
typedef struct {
    unsigned long record_id;
    char book_id[20];
    char copy_id[20];
    char book_title[80];
    char member_id[20];
    char member_name[50];
    char action[10];
    time_t timestamp;
    time_t due_date;
    int fine_amount;
    unsigned long nonce;
    unsigned char signature[SIGNATURE_MAX_LEN];
    unsigned int sig_len;
} LendingRecord;

typedef struct {
    int index;
    time_t timestamp;
    int record_count;
    LendingRecord records[MAX_RECORDS_PER_BLOCK];
    char previous_hash[65];
    char merkle_root[65];
    char hash[65];
    char sealed_by[SEALED_BY_MAX];
    unsigned long nonce;
    int difficulty;
    int token_reward;
    char transaction_id[65];
} Block;

typedef struct {
    Block *blocks;
    int count;
    int capacity;
} Blockchain;

void blockchain_init(Blockchain *chain);
void blockchain_free(Blockchain *chain);

/* Deep-copies src into dst (dst must not already hold live memory - it
 * gets its own freshly malloc'd block array). Free dst with
 * blockchain_free once done with it, same as any other Blockchain. */
void blockchain_clone(const Blockchain *src, Blockchain *dst);

void blockchain_append_confirmed(Blockchain *chain, const Block *b);

int blockchain_add_genesis(Blockchain *chain);

/* Signs a record's content fields (everything except the signature
 * itself) with the given key. Called before a request enters the
 * pending pool, so forging a request without the matching private key
 * is impossible. */
int blockchain_sign_record(LendingRecord *r, EVP_PKEY *pkey);

int blockchain_verify_record(const LendingRecord *r, EVP_PKEY *pkey);

/* Fills in a block's header fields from a batch of already-signed
 * records (index, previous_hash, merkle_root, record_count, difficulty),
 * leaving nonce/hash for the miner's proof-of-work search to finish.
 * timestamp is time(NULL) clamped to strictly after previous_timestamp,
 * so blocks mined faster than one real second apart still satisfy the
 * chain's timestamp-consistency rule. */
void blockchain_seal_block(Block *b, const LendingRecord *records, int count,
                            int index, const char *previous_hash, time_t previous_timestamp,
                            const char *sealed_by, int difficulty);

void blockchain_recompute_hash(const Block *b, char out_hex[HASH_HEX_LEN]);

/* Searches every confirmed block's records, most recent first, for the
 * last record touching this copy_id. Returns NULL if the copy has no
 * history yet. */
const LendingRecord *blockchain_last_action_for_copy(const Blockchain *chain, const char *copy_id);

/* Verifies every block's hash, previous-hash link, merkle root, stored
 * proof-of-work (hash actually has `difficulty` leading zero hex chars),
 * strictly-increasing timestamp, and each record's signature against its
 * own signer's public key (looked up by member_id in keys) rather than
 * one shared key - a record forged under a different member's name fails
 * verification even if every field otherwise looks plausible. */
int blockchain_validate(const Blockchain *chain, MemberKeyStore *keys);

void blockchain_print_records(const Blockchain *chain, MemberKeyStore *keys);

#endif
