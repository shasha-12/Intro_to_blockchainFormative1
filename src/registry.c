#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "registry.h"

static void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

static int split_csv(char *line, char fields[][80], int field_size, int max_fields) {
    int count = 0;
    char *token = strtok(line, ",");
    while (token != NULL && count < max_fields) {
        strncpy(fields[count], token, (size_t)field_size - 1);
        fields[count][field_size - 1] = '\0';
        count++;
        token = strtok(NULL, ",");
    }
    return count;
}

/* Registries are the trust boundary for the whole program: every borrow or
 * return checks its book/member ID against what got loaded here, so a
 * malformed line is skipped with a warning rather than silently accepted
 * or crashing the loader. */
static int load_books(Registry *reg, const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: Could not open book registry file '%s'\n", path);
        return 0;
    }

    char line[256];
    reg->book_count = 0;
    while (fgets(line, sizeof(line), fp) != NULL && reg->book_count < MAX_BOOKS) {
        trim_newline(line);
        if (line[0] == '\0') continue;

        char fields[3][80] = {{0}};
        char buf[256];
        strncpy(buf, line, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        int n = split_csv(buf, fields, 80, 3);
        if (n < 3) {
            fprintf(stderr, "WARNING: Skipping malformed book entry: %s\n", line);
            continue;
        }

        Book *b = &reg->books[reg->book_count];
        strncpy(b->book_id, fields[0], sizeof(b->book_id) - 1);
        b->book_id[sizeof(b->book_id) - 1] = '\0';
        strncpy(b->title, fields[1], sizeof(b->title) - 1);
        b->title[sizeof(b->title) - 1] = '\0';
        strncpy(b->author, fields[2], sizeof(b->author) - 1);
        b->author[sizeof(b->author) - 1] = '\0';
        reg->book_count++;
    }
    fclose(fp);

    if (reg->book_count == 0) {
        fprintf(stderr, "ERROR: Book registry file '%s' is empty\n", path);
        return 0;
    }
    return 1;
}

static int load_members(Registry *reg, const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: Could not open member registry file '%s'\n", path);
        return 0;
    }

    char line[256];
    reg->member_count = 0;
    while (fgets(line, sizeof(line), fp) != NULL && reg->member_count < MAX_MEMBERS) {
        trim_newline(line);
        if (line[0] == '\0') continue;

        char fields[3][80] = {{0}};
        char buf[256];
        strncpy(buf, line, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        int n = split_csv(buf, fields, 80, 3);
        if (n < 3) {
            fprintf(stderr, "WARNING: Skipping malformed member entry: %s\n", line);
            continue;
        }

        Member *m = &reg->members[reg->member_count];
        strncpy(m->member_id, fields[0], sizeof(m->member_id) - 1);
        m->member_id[sizeof(m->member_id) - 1] = '\0';
        strncpy(m->full_name, fields[1], sizeof(m->full_name) - 1);
        m->full_name[sizeof(m->full_name) - 1] = '\0';
        strncpy(m->course_code, fields[2], sizeof(m->course_code) - 1);
        m->course_code[sizeof(m->course_code) - 1] = '\0';
        reg->member_count++;
    }
    fclose(fp);

    if (reg->member_count == 0) {
        fprintf(stderr, "ERROR: Member registry file '%s' is empty\n", path);
        return 0;
    }
    return 1;
}

int registry_load(Registry *reg, const char *books_path, const char *members_path) {
    int books_ok = load_books(reg, books_path);
    int members_ok = load_members(reg, members_path);
    return books_ok && members_ok;
}

const Book *registry_find_book(const Registry *reg, const char *book_id) {
    for (int i = 0; i < reg->book_count; i++) {
        if (strcmp(reg->books[i].book_id, book_id) == 0) {
            return &reg->books[i];
        }
    }
    return NULL;
}

const Member *registry_find_member(const Registry *reg, const char *member_id) {
    for (int i = 0; i < reg->member_count; i++) {
        if (strcmp(reg->members[i].member_id, member_id) == 0) {
            return &reg->members[i];
        }
    }
    return NULL;
}
