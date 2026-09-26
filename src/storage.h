#ifndef STORAGE_H
#define STORAGE_H

#include "blockchain.h"

int storage_load_chain(Blockchain *chain, const char *path);

int storage_save_chain(const Blockchain *chain, const char *path);

#endif
