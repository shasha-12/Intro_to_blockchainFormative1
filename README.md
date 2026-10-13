# Blockchain-Based Library Lending Tracker

A C implementation of a small blockchain used to run a library's lending
desk end to end: book/copy inventory, reservations, renewals and overdue
fines, borrow/return requests signed individually by each member, mined
into multi-record blocks with a Merkle root, replay-protected by a
per-member nonce, and screened by a handful of fraud heuristics before
they're ever confirmed.

This is the Summative project, combining and extending Formative 1
(registries, blocks, hashing, signatures, chain validation) and
Formative 2 (pending pool, proof-of-work mining, token rewards, UTXO and
account-based ledgers) into one system. `main` and `formative-2` are left
untouched as submission references for those two assignments; this
branch is where the combined system and its extensions live.

## Features

**Chain & Merkle tree**
- Each block batches up to 8 lending records (`MAX_RECORDS_PER_BLOCK`,
  the documented fixed-size batching policy) instead of one record per
  block.
- Every record is signed individually by whoever requested it (the
  member, via their own key - see below), not the block as a whole.
  A block's `merkle_root` is built from scratch (no external library)
  over all of its records, with each leaf hash covering the record's
  own signature, so forging a signature also breaks the root.
- `validate_chain` recomputes every block's hash, hash link, and Merkle
  root, and verifies every record's signature against its own signer.
- `tamper_block` mutates a past record in place (no re-sign) so
  `validate_chain` can be shown catching it - both the hash and the
  Merkle root mismatch, proving the per-record layer is doing real work.

**Copy inventory & reservations**
- Physical copies (`copies.txt`) are tracked individually, each with its
  own state: `AVAILABLE -> BORROWED -> AVAILABLE` or `RESERVED` (if
  someone's waiting) `-> BORROWED` (reservation pickup), or `-> LOST`
  (`mark_lost`, which requires a reason code).
- A FIFO reservation queue per book: `reserve_book` joins it,
  `return_book` auto-consumes the front of the queue and holds that
  exact copy for the next member in line.

**Renewals, overdue detection, fines**
- `renew_book` extends a loan's due date, up to `MAX_RENEWALS` times, and
  is blocked if another member is waiting on the title.
- `check_overdue` (also run automatically at startup) flags any
  past-due loan and accrues a fine per simulated day late, kept in a
  separate `FineLedger` since fines aren't blockchain token transfers.
  `view_fines` / `settle_fine` round it out.

**Per-member identity & replay protection**
- Every member has their own ECDSA (P-256) keypair, generated the first
  time they're looked up and reused from `keys/members/` after that -
  replacing the single shared librarian key Formative 1/2 used for
  everything. `register_member` / `view_member` manage this.
- A `NonceRegistry` tracks each member's *confirmed* nonce - it only
  advances when a request is actually mined. New requests are assigned
  `confirmed nonce + that member's own pending request count`, so two of
  a member's own unconfirmed actions never collide with each other.
  Mining rejects (and leaves in the pool) any record whose nonce doesn't
  match what's expected. `replay_test` resubmits an already-confirmed
  record to demonstrate this actually rejecting a replay.

**Pending pool priority ordering**
- The pool doesn't just hold records in arrival order - `pool_view` and
  every mining mode re-sort it first: overdue-related requests, then
  returns a book someone's still waiting on (earliest reservation date
  first), then everything else, keeping ties in their original order.

**Misuse / fraud detection**
- A new request is flagged `SUSPICIOUS` instead of silently accepted
  when it matches one of four heuristics: excessive concurrent borrows,
  a rapid re-borrow of the same copy within one simulated day, a
  duplicate `request_id` already seen in the pool or on-chain, or a
  reported condition downgrade on return with no librarian override.
  Flagged requests sit out of mining until reviewed: `fraud_review`
  lists them, `approve_suspicious <id>` / `reject_suspicious <id>`
  decide them.

**Audit commands**
- `lending_history`, `member_history <id>`, and `book_history <id>` walk
  the confirmed chain and the pending pool together, labeling each row
  `CONFIRMED` / `PENDING` / `SUSPICIOUS`.

**Pending pool & mining (carried over from Formative 2, updated for
multi-record blocks)**
- Three mining modes, all operating on the same pending pool:
  - **Solo** - real proof-of-work (nonce search until the hash has N
    leading zero hex characters), adjustable difficulty (1-4).
  - **Pool** - the same proof-of-work, with the resulting reward (minus
    a 2% pool fee) split across simulated miners by a randomly weighted
    share of the total hash attempts.
  - **Cloud** - rents hash power over 1-5 rounds, tracking a fixed
    rental fee against a randomized reward each round, with a running
    net-profit total and a warning if the rental goes unprofitable.
- A confirmed `RETURNED` record earns its member 10 coins on time, 5
  late, aggregated per block into that block's `token_reward`.

**Token ledgers (carried over from Formative 2)**
- **UTXO model** - unspent outputs, inputs/outputs, a 1-coin fee per
  transfer, change outputs, double-spend prevention.
- **Account model** - per-member balance and nonce, plus an in-memory
  per-account transaction history (by design - not persisted).
- Chosen once per session (startup prompt or `--model` flag).

**Persistence**
- Chain, pending pool, UTXO set, account ledger, copy inventory,
  reservation queue, fine ledger, and the nonce registry all survive a
  restart (`chain.dat`, `pending_pool.dat`, `utxo_set.dat`,
  `account_ledger.dat`, `copy_inventory.dat`, `reservation_queue.dat`,
  `fine_ledger.dat`, `nonce_registry.dat`). Per-account transaction
  *history* is intentionally in-memory only, per the assignment's own
  description of it as such.
- Member keypairs are generated lazily and reused from `keys/members/`.

> **Simulated time scales:** a 15-second loan period, 15-second renewal
> window, and 5-second "day" (for overdue/fine calculations and the
> rapid-reborrow check) stand in for what would be days/weeks in a real
> system - not demoable inside a short video otherwise. Deliberate
> simulation shortcuts, documented as such in the technical report.

## Required libraries / dependencies

No new dependencies beyond Formative 1/2:

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

Optional flags (both can be combined, and both can also be changed from
inside the CLI):

```bash
./library_tracker --difficulty 3          # mining difficulty, 1-4 (default 2)
./library_tracker --model account         # 'utxo' (default) or 'account'
./library_tracker --difficulty 3 --model account
```

On first run the program will:
1. Load `books.txt` and `members.txt` (exits with an error if either is
   missing or empty).
2. Load `copies.txt` into a fresh copy inventory (reused on later runs
   via `copy_inventory.dat`).
3. Create a genesis block and persist the chain to `chain.dat` (reused on
   later runs), along with any pending pool / ledger / reservation /
   fine / nonce state from a previous session.
4. Run an automatic overdue check.
5. Prompt for a transaction model (UTXO or account-based) unless one was
   given via `--model` - this choice is locked for the rest of the
   session.

Member keypairs are generated the first time each member is looked up
(borrowing, returning, etc.), not all at startup.

## Commands

The CLI takes one command per line, with an optional inline argument for
a few of them (`mine_cloud <rounds>`, `mark_lost <copy_id>`,
`transaction_history <member_id>`, `member_history <id>`,
`book_history <id>`, `approve_suspicious <id>`, `reject_suspicious <id>`)
- otherwise it prompts for whatever it needs.

```
Membership:   register_member  view_member  member_nonce
Book/Copy:    register_book  view_book  add_copy  view_copy  mark_lost
Lending:      borrow_book  return_book  renew_book  reserve_book  reservation_status
Fines:        check_overdue  view_fines  settle_fine
Fraud:        fraud_review  approve_suspicious <id>  reject_suspicious <id>
Pool/Mining:  pool_view  mine_solo  mine_pool  mine_cloud <rounds>
              difficulty_status  set_difficulty
Chain:        chain_view  validate_chain  chain_save  chain_load  tamper_block  replay_test
Audit:        lending_history  member_history <id>  book_history <id>
Tokens:       utxo_view  token_transfer  token_balance  credit_test_utxo
              account_balance  account_transfer  account_nonce
              transaction_history <member_id>  credit_test_account
System:       help  exit
```

`credit_test_utxo` / `credit_test_account` are manual dev/test hooks for
setting up balances without waiting on a real borrow/return/mine cycle.
`replay_test <copy_id>` is a similar hook that resubmits an already-
confirmed record to demonstrate nonce-replay rejection and duplicate-
request fraud detection actually firing.

### Quick end-to-end walkthrough

```
borrow_book           -> enter a valid book_id/member_id
return_book            -> return it (immediately = on-time, wait 15+
                           seconds first = late, with a fine if overdue)
pool_view               -> see the unconfirmed records waiting, in
                           priority order
mine_solo               -> real proof-of-work, confirms them, credits
                           the return's token reward and the miner's
                           block reward
validate_chain          -> hash / link / Merkle root / signatures, all OK
lending_history          -> the full confirmed + pending activity log
```

Swap `mine_solo` for `mine_pool` (choose how many simulated miners) or
`mine_cloud <rounds>` (choose a renter ID and 1-5 rounds) to see the
other two mining simulations.

### How to switch transaction models

Either pass `--model utxo` / `--model account` on the command line, or
answer the startup prompt (`1` for UTXO, `2` for account-based, UTXO is
the default on a blank answer). Whichever you pick is what reward
crediting and mining rewards go through for the whole run.

### How to set the mining difficulty

Either `--difficulty N` at startup, or `set_difficulty` at any time
(1-4, default 2). Higher difficulty means more leading zero hex
characters required in a block's hash, which means more proof-of-work
attempts on average - all three mining commands use whatever difficulty
is currently set (`difficulty_status` shows it).

## File layout

```
src/
  main.c              CLI command loop, model selection, all command handlers
  registry.c/h         Book/Member structs, file loaders, lookups
  crypto.c/h           SHA-256, ECDSA sign/verify, transaction-id hashing (OpenSSL)
  blockchain.c/h       LendingRecord/Block structs, signing, hashing, chain validation
  merkle.c/h           From-scratch Merkle tree construction over a block's records
  member_keys.c/h       Per-member ECDSA keypair store (lazy-generated)
  nonce_registry.c/h    Per-member confirmed-nonce tracking (replay protection)
  inventory.c/h         Copy inventory (state machine) and the reservation queue
  fines.c/h             Fine ledger (accrue/settle/owed)
  pending_pool.c/h      Unconfirmed records: priority ordering, fraud flagging
  mining.c/h            Proof-of-work, nonce-checked batch gathering, solo/pool/cloud
  utxo.c/h             UTXO set, mint/transfer, fees, change outputs
  account.c/h          Per-member balances, nonces, linked-list history
  storage.c/h           Binary persistence for all of the above
books.txt             Sample book registry
members.txt           Sample member registry
copies.txt            Sample physical copy inventory
keys/members/         Generated per-member ECDSA keypairs (gitignored)
```

## Resetting to a clean state

```bash
rm -f chain.dat pending_pool.dat utxo_set.dat account_ledger.dat \
      copy_inventory.dat reservation_queue.dat fine_ledger.dat \
      nonce_registry.dat
rm -rf keys/members
```

This wipes the persisted chain, pending pool, both ledgers, the copy
inventory/reservations/fines, the nonce registry, and every generated
member keypair, so the next run starts completely fresh from
`books.txt` / `members.txt` / `copies.txt`.
