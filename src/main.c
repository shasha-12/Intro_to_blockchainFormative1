#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "registry.h"
#include "crypto.h"
#include "blockchain.h"
#include "storage.h"

#define BOOKS_FILE "books.txt"
#define MEMBERS_FILE "members.txt"
#define CHAIN_FILE "chain.dat"
#define PRIV_KEY_FILE "keys/private.pem"
#define PUB_KEY_FILE "keys/public.pem"

static void read_line(char *buf, size_t size) {
    if (fgets(buf, (int)size, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
}

static void print_menu(void) {
    printf("\n===== Library Lending Tracker =====\n");
    printf("1. Borrow Book\n");
    printf("2. Return Book\n");
    printf("3. View Lending Records\n");
    printf("4. Validate Chain\n");
    printf("5. Tamper With a Past Block (Demo)\n");
    printf("6. Exit\n");
    printf("Choose an option: ");
}

static void handle_borrow(Blockchain *chain, const Registry *reg, EVP_PKEY *pkey) {
    char book_id[20], member_id[20];

    printf("Enter Book ID: ");
    read_line(book_id, sizeof(book_id));
    printf("Enter Member ID: ");
    read_line(member_id, sizeof(member_id));

    const Book *book = registry_find_book(reg, book_id);
    const Member *member = registry_find_member(reg, member_id);

    if (book == NULL || member == NULL) {
        printf("ERROR: Book or Member not found\n");
        return;
    }

    const Block *last = blockchain_last_action_for_book(chain, book_id);
    if (last != NULL && strcmp(last->action, ACTION_BORROWED) == 0) {
        printf("ERROR: '%s' is already on loan and has not been returned\n", book->title);
        return;
    }

    if (!blockchain_add_block(chain, pkey, book->book_id, book->title,
                               member->member_id, member->full_name, ACTION_BORROWED)) {
        printf("ERROR: Failed to record borrow transaction\n");
        return;
    }

    storage_save_chain(chain, CHAIN_FILE);
    printf("OK: '%s' borrowed by %s\n", book->title, member->full_name);
}

static void handle_return(Blockchain *chain, EVP_PKEY *pkey) {
    char book_id[20];
    printf("Enter Book ID: ");
    read_line(book_id, sizeof(book_id));

    const Block *last = blockchain_last_action_for_book(chain, book_id);
    if (last == NULL || strcmp(last->action, ACTION_BORROWED) != 0) {
        printf("ERROR: No active loan found for book ID '%s'\n", book_id);
        return;
    }

    if (!blockchain_add_block(chain, pkey, last->book_id, last->book_title,
                               last->member_id, last->member_name, ACTION_RETURNED)) {
        printf("ERROR: Failed to record return transaction\n");
        return;
    }

    storage_save_chain(chain, CHAIN_FILE);
    printf("OK: '%s' returned by %s\n", last->book_title, last->member_name);
}

static void handle_tamper_demo(Blockchain *chain) {
    if (chain->count <= 1) {
        printf("No lending blocks yet to tamper with (only the genesis block exists).\n");
        return;
    }

    printf("Enter the index of the block to tamper with (1-%d): ", chain->count - 1);
    char input[16];
    read_line(input, sizeof(input));
    int idx = atoi(input);

    if (idx < 1 || idx >= chain->count) {
        printf("ERROR: Invalid block index\n");
        return;
    }

    Block *b = &chain->blocks[idx];
    printf("Tampering with block %d: book_title '%s' -> 'TAMPERED RECORD' (without re-signing)\n",
           idx, b->book_title);
    strncpy(b->book_title, "TAMPERED RECORD", sizeof(b->book_title) - 1);
    b->book_title[sizeof(b->book_title) - 1] = '\0';

    storage_save_chain(chain, CHAIN_FILE);
    printf("Block %d modified in memory and on disk. Run 'Validate Chain' to see it detected.\n", idx);
}

int main(void) {
    Registry reg;
    if (!registry_load(&reg, BOOKS_FILE, MEMBERS_FILE)) {
        fprintf(stderr, "FATAL: Could not start — book/member registries missing or empty.\n");
        return 1;
    }
    printf("Loaded %d books and %d members.\n", reg.book_count, reg.member_count);

    EVP_PKEY *pkey = NULL;
    if (!crypto_load_or_generate_keys(PRIV_KEY_FILE, PUB_KEY_FILE, &pkey)) {
        fprintf(stderr, "FATAL: Could not load or generate signing keys.\n");
        return 1;
    }

    Blockchain chain;
    blockchain_init(&chain);
    if (!storage_load_chain(&chain, CHAIN_FILE)) {
        printf("No existing chain found — creating genesis block.\n");
        blockchain_add_genesis(&chain, pkey);
        storage_save_chain(&chain, CHAIN_FILE);
    } else {
        printf("Loaded existing chain with %d block(s).\n", chain.count);
    }

    int running = 1;
    while (running) {
        print_menu();
        char input[16];
        read_line(input, sizeof(input));
        int choice = atoi(input);

        switch (choice) {
            case 1:
                handle_borrow(&chain, &reg, pkey);
                break;
            case 2:
                handle_return(&chain, pkey);
                break;
            case 3:
                blockchain_print_records(&chain, pkey);
                break;
            case 4: {
                int valid = blockchain_validate(&chain, pkey);
                printf(valid ? "\nChain is VALID — no tampering detected.\n"
                             : "\nChain is INVALID — tampering detected!\n");
                break;
            }
            case 5:
                handle_tamper_demo(&chain);
                break;
            case 6:
                running = 0;
                break;
            default:
                printf("Invalid option. Please choose 1-6.\n");
        }
    }

    blockchain_free(&chain);
    crypto_free_key(pkey);
    return 0;
}
