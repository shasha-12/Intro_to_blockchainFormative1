CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Isrc
LDFLAGS = -lcrypto

OPENSSL_PREFIX := $(shell brew --prefix openssl@3 2>/dev/null)
ifneq ($(OPENSSL_PREFIX),)
    CFLAGS += -I$(OPENSSL_PREFIX)/include
    LDFLAGS += -L$(OPENSSL_PREFIX)/lib
endif

SRC = src/main.c src/registry.c src/crypto.c src/blockchain.c src/storage.c
OBJ = $(SRC:.c=.o)
TARGET = library_tracker

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET)
