#include <stdio.h>
#include <stdlib.h>
#include "storage.h"

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
