# Blockchain-Based Library Book Lending Tracker

A C implementation of a small blockchain used to record book borrow/return
events. Every lending action becomes a hash-linked, ECDSA-signed block, so a
librarian cannot quietly alter a past record without breaking the chain.

## Features

- Book and member registries (`books.txt`, `members.txt`) loaded into memory
  at startup and validated before any lending action is recorded.
- A genesis-anchored blockchain where every block is:
  - linked to the previous block via its SHA-256 hash (`previous_hash`),
  - signed with an ECDSA (P-256) private key,
  - hashed as a whole (including the signature) to produce its own `hash`.
- Chain validation that recomputes every hash, checks every hash link, and
  verifies every signature.
- File-based persistence: the chain survives restarts (`chain.dat`), and the
  signing keypair is generated once and reused (`keys/private.pem`,
  `keys/public.pem`).
- A CLI to borrow, return, view records, validate the chain, and a demo
  option to tamper with a past block and watch validation catch it.

## Required libraries / dependencies

- A C11 compiler (`gcc` or `clang`)
- `make`
- **OpenSSL 3.x development files** (used for `SHA256`, `EVP_PKEY` ECDSA
  sign/verify, and PEM key storage)

On macOS (Homebrew):

```bash
brew install openssl@3
```

On Debian/Ubuntu:

```bash
sudo apt-get install build-essential libssl-dev
```

## Build

```bash
make
```

The Makefile auto-detects Homebrew's `openssl@3` prefix on macOS (it's
keg-only, so it isn't on the default include/library path). On Linux with
`libssl-dev` installed system-wide, no extra flags are needed.

## Run

```bash
./library_tracker
# or
make run
```

On first run the program will:
1. Load `books.txt` and `members.txt` (exits with an error if either is
   missing or empty).
2. Generate an ECDSA P-256 keypair into `keys/` (reused on later runs).
3. Create a genesis block and persist the chain to `chain.dat` (reused/loaded
   on later runs).

### Menu

```
1. Borrow Book              -- validates book_id/member_id against the
                                registries, rejects if the book is already
                                on loan, then appends a signed BORROWED block
2. Return Book               -- finds the book's most recent BORROWED block
                                 and appends a signed RETURNED block
3. View Lending Records      -- prints every block with signature validity
4. Validate Chain            -- recomputes hashes/links/signatures for the
                                 whole chain and reports OK/BROKEN per block
5. Simulate Tampering (Demo) -- mutates a past block in place (no re-sign)
                                 so Validate Chain can be shown catching it
6. Exit
```

## File layout

```
src/
  main.c        CLI menu loop
  registry.c/h  Book/Member structs, file loaders, lookups
  crypto.c/h    SHA-256 wrapper, ECDSA keypair gen/load/sign/verify (OpenSSL)
  blockchain.c/h Block struct, hashing, signing, chain validation
  storage.c/h   Binary persistence of the chain to chain.dat
books.txt       Sample book registry
members.txt     Sample member registry
keys/           Generated ECDSA keypair (gitignored; created at first run)
```

## Resetting to a clean state

```bash
rm -f chain.dat keys/private.pem keys/public.pem
```

This wipes the persisted chain and signing key so the next run starts fresh
with a new genesis block and a new keypair.
