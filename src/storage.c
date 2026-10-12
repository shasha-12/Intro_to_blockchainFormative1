#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h"

/* The chain, pending pool, and UTXO set are all plain-data structs (no
 * pointers), so each one persists the same way: a count header, then the
 * raw array written with a single fwrite. This only works because none of
 * Block or UTXO contains a pointer - the one struct that does (Account,
 * via its `history` list) needs the separate AccountRecord handling further
 * down instead. */
int storage_load_chain(Blockchain *chain, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }

    int count = 0;
    if (fread(&count, sizeof(int), 1, fp) != 1 || count < 0) {
        fprintf(stderr, "ERROR: Chain file '%s' is corrupted (bad header)\n", path);
        fclose(fp);
        return 0;
    }

    if (count > chain->capacity) {
        chain->capacity = count > 0 ? count : 1;
        chain->blocks = realloc(chain->blocks, sizeof(Block) * (size_t)chain->capacity);
    }

    if (count > 0 && fread(chain->blocks, sizeof(Block), (size_t)count, fp) != (size_t)count) {
        fprintf(stderr, "ERROR: Chain file '%s' is corrupted (truncated data)\n", path);
        fclose(fp);
        chain->count = 0;
        return 0;
    }

    chain->count = count;
    fclose(fp);
    return 1;
}

int storage_save_chain(const Blockchain *chain, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: Could not write chain file '%s'\n", path);
        return 0;
    }

    int ok = fwrite(&chain->count, sizeof(int), 1, fp) == 1;
    if (ok && chain->count > 0) {
        ok = fwrite(chain->blocks, sizeof(Block), (size_t)chain->count, fp) == (size_t)chain->count;
    }

    fclose(fp);
    if (!ok) {
        fprintf(stderr, "ERROR: Failed to write full chain to '%s'\n", path);
    }
    return ok;
}

int storage_load_pending_pool(PendingPool *pool, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }

    int count = 0;
    if (fread(&count, sizeof(int), 1, fp) != 1 || count < 0) {
        fprintf(stderr, "ERROR: Pending pool file '%s' is corrupted (bad header)\n", path);
        fclose(fp);
        return 0;
    }

    if (count > pool->capacity) {
        pool->capacity = count > 0 ? count : 1;
        pool->entries = realloc(pool->entries, sizeof(LendingRecord) * (size_t)pool->capacity);
    }

    if (count > 0 && fread(pool->entries, sizeof(LendingRecord), (size_t)count, fp) != (size_t)count) {
        fprintf(stderr, "ERROR: Pending pool file '%s' is corrupted (truncated data)\n", path);
        fclose(fp);
        pool->count = 0;
        return 0;
    }

    pool->count = count;
    fclose(fp);
    return 1;
}

int storage_save_pending_pool(const PendingPool *pool, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: Could not write pending pool file '%s'\n", path);
        return 0;
    }

    int ok = fwrite(&pool->count, sizeof(int), 1, fp) == 1;
    if (ok && pool->count > 0) {
        ok = fwrite(pool->entries, sizeof(LendingRecord), (size_t)pool->count, fp) == (size_t)pool->count;
    }

    fclose(fp);
    if (!ok) {
        fprintf(stderr, "ERROR: Failed to write full pending pool to '%s'\n", path);
    }
    return ok;
}

int storage_load_utxo_set(UTXOSet *set, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }

    int count = 0;
    if (fread(&count, sizeof(int), 1, fp) != 1 || count < 0) {
        fprintf(stderr, "ERROR: UTXO set file '%s' is corrupted (bad header)\n", path);
        fclose(fp);
        return 0;
    }

    if (count > set->capacity) {
        set->capacity = count > 0 ? count : 1;
        set->items = realloc(set->items, sizeof(UTXO) * (size_t)set->capacity);
    }

    if (count > 0 && fread(set->items, sizeof(UTXO), (size_t)count, fp) != (size_t)count) {
        fprintf(stderr, "ERROR: UTXO set file '%s' is corrupted (truncated data)\n", path);
        fclose(fp);
        set->count = 0;
        return 0;
    }

    set->count = count;
    fclose(fp);
    return 1;
}

int storage_save_utxo_set(const UTXOSet *set, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: Could not write UTXO set file '%s'\n", path);
        return 0;
    }

    int ok = fwrite(&set->count, sizeof(int), 1, fp) == 1;
    if (ok && set->count > 0) {
        ok = fwrite(set->items, sizeof(UTXO), (size_t)set->count, fp) == (size_t)set->count;
    }

    fclose(fp);
    if (!ok) {
        fprintf(stderr, "ERROR: Failed to write full UTXO set to '%s'\n", path);
    }
    return ok;
}

/* Account has a `history` pointer, so it can't be fwrite'd/fread'd
 * directly like the other structs in this file - a pointer written to
 * disk is meaningless once reloaded into a new process. This mirrors
 * just the three plain-data fields that are safe to persist. */
typedef struct {
    char member_id[20];
    int balance;
    unsigned long nonce;
} AccountRecord;

int storage_load_account_ledger(AccountLedger *ledger, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }

    int count = 0;
    if (fread(&count, sizeof(int), 1, fp) != 1 || count < 0) {
        fprintf(stderr, "ERROR: Account ledger file '%s' is corrupted (bad header)\n", path);
        fclose(fp);
        return 0;
    }

    for (int i = 0; i < count; i++) {
        AccountRecord rec;
        if (fread(&rec, sizeof(AccountRecord), 1, fp) != 1) {
            fprintf(stderr, "ERROR: Account ledger file '%s' is corrupted (truncated data)\n", path);
            fclose(fp);
            return 0;
        }
        Account *a = account_get_or_create(ledger, rec.member_id);
        a->balance = rec.balance;
        a->nonce = rec.nonce;
    }

    fclose(fp);
    return 1;
}

int storage_save_account_ledger(const AccountLedger *ledger, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: Could not write account ledger file '%s'\n", path);
        return 0;
    }

    int ok = fwrite(&ledger->count, sizeof(int), 1, fp) == 1;
    for (int i = 0; ok && i < ledger->count; i++) {
        AccountRecord rec;
        strncpy(rec.member_id, ledger->accounts[i].member_id, sizeof(rec.member_id) - 1);
        rec.member_id[sizeof(rec.member_id) - 1] = '\0';
        rec.balance = ledger->accounts[i].balance;
        rec.nonce = ledger->accounts[i].nonce;
        ok = fwrite(&rec, sizeof(AccountRecord), 1, fp) == 1;
    }

    fclose(fp);
    if (!ok) {
        fprintf(stderr, "ERROR: Failed to write full account ledger to '%s'\n", path);
    }
    return ok;
}

int storage_load_copy_inventory(CopyInventory *inv, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return 0;

    int count = 0;
    if (fread(&count, sizeof(int), 1, fp) != 1 || count < 0) {
        fclose(fp);
        return 0;
    }
    if (count > inv->capacity) {
        inv->capacity = count > 0 ? count : 1;
        inv->copies = realloc(inv->copies, sizeof(Copy) * (size_t)inv->capacity);
    }
    if (count > 0 && fread(inv->copies, sizeof(Copy), (size_t)count, fp) != (size_t)count) {
        fclose(fp);
        return 0;
    }
    inv->count = count;
    fclose(fp);
    return 1;
}

int storage_save_copy_inventory(const CopyInventory *inv, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) return 0;

    int ok = fwrite(&inv->count, sizeof(int), 1, fp) == 1;
    if (ok && inv->count > 0) {
        ok = fwrite(inv->copies, sizeof(Copy), (size_t)inv->count, fp) == (size_t)inv->count;
    }
    fclose(fp);
    return ok;
}

int storage_load_reservation_queue(ReservationQueue *q, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return 0;

    int count = 0;
    if (fread(&count, sizeof(int), 1, fp) != 1 || count < 0) {
        fclose(fp);
        return 0;
    }
    if (count > q->capacity) {
        q->capacity = count > 0 ? count : 1;
        q->entries = realloc(q->entries, sizeof(Reservation) * (size_t)q->capacity);
    }
    if (count > 0 && fread(q->entries, sizeof(Reservation), (size_t)count, fp) != (size_t)count) {
        fclose(fp);
        return 0;
    }
    q->count = count;
    fclose(fp);
    return 1;
}

int storage_save_reservation_queue(const ReservationQueue *q, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) return 0;

    int ok = fwrite(&q->count, sizeof(int), 1, fp) == 1;
    if (ok && q->count > 0) {
        ok = fwrite(q->entries, sizeof(Reservation), (size_t)q->count, fp) == (size_t)q->count;
    }
    fclose(fp);
    return ok;
}

int storage_load_fine_ledger(FineLedger *ledger, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return 0;

    int count = 0;
    if (fread(&count, sizeof(int), 1, fp) != 1 || count < 0) {
        fclose(fp);
        return 0;
    }
    if (count > ledger->capacity) {
        ledger->capacity = count > 0 ? count : 1;
        ledger->accounts = realloc(ledger->accounts, sizeof(FineAccount) * (size_t)ledger->capacity);
    }
    if (count > 0 && fread(ledger->accounts, sizeof(FineAccount), (size_t)count, fp) != (size_t)count) {
        fclose(fp);
        return 0;
    }
    ledger->count = count;
    fclose(fp);
    return 1;
}

int storage_save_fine_ledger(const FineLedger *ledger, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) return 0;

    int ok = fwrite(&ledger->count, sizeof(int), 1, fp) == 1;
    if (ok && ledger->count > 0) {
        ok = fwrite(ledger->accounts, sizeof(FineAccount), (size_t)ledger->count, fp) == (size_t)ledger->count;
    }
    fclose(fp);
    return ok;
}

int storage_load_nonce_registry(NonceRegistry *reg, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return 0;

    int count = 0;
    if (fread(&count, sizeof(int), 1, fp) != 1 || count < 0) {
        fclose(fp);
        return 0;
    }
    if (count > reg->capacity) {
        reg->capacity = count > 0 ? count : 1;
        reg->entries = realloc(reg->entries, sizeof(MemberNonce) * (size_t)reg->capacity);
    }
    if (count > 0 && fread(reg->entries, sizeof(MemberNonce), (size_t)count, fp) != (size_t)count) {
        fclose(fp);
        return 0;
    }
    reg->count = count;
    fclose(fp);
    return 1;
}

int storage_save_nonce_registry(const NonceRegistry *reg, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) return 0;

    int ok = fwrite(&reg->count, sizeof(int), 1, fp) == 1;
    if (ok && reg->count > 0) {
        ok = fwrite(reg->entries, sizeof(MemberNonce), (size_t)reg->count, fp) == (size_t)reg->count;
    }
    fclose(fp);
    return ok;
}
