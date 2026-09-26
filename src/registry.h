#ifndef REGISTRY_H
#define REGISTRY_H

#define MAX_BOOKS 200
#define MAX_MEMBERS 200

typedef struct {
    char book_id[20];
    char title[80];
    char author[50];
} Book;

typedef struct {
    char member_id[20];
    char full_name[50];
    char course_code[10];
} Member;

typedef struct {
    Book books[MAX_BOOKS];
    int book_count;
    Member members[MAX_MEMBERS];
    int member_count;
} Registry;

int registry_load(Registry *reg, const char *books_path, const char *members_path);

const Book *registry_find_book(const Registry *reg, const char *book_id);

const Member *registry_find_member(const Registry *reg, const char *member_id);

#endif
