#ifndef MERKLE_H
#define MERKLE_H

#include "blockchain.h"

/* SHA-256 over every field of one record, including its signature, so
 * a forged signature (not just a forged field) also changes the leaf
 * hash and gets caught when the root is recomputed. */
void merkle_leaf_hash(const LendingRecord *r, char out_hex[HASH_HEX_LEN]);

/* Builds a merkle tree from scratch over count records and writes the
 * root into out_root. An empty block (count == 0, i.e. genesis) gets a
 * root of 64 zeros, matching the convention already used for an empty
 * previous_hash. An odd node at any level is paired with itself. */
void merkle_compute_root(const LendingRecord *records, int count, char out_root[HASH_HEX_LEN]);

#endif
