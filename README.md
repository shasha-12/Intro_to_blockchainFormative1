# Blockchain-Based Library Book Lending Tracker

A C implementation of a small blockchain used to record book borrow/return
events. Every lending action becomes a hash-linked, ECDSA-signed block, so a
librarian cannot quietly alter a past record without breaking the chain.

This started as Formative 1 (registries, blocks, hashing, signatures, chain
validation) and was extended for Formative 2 to add a pending-block pool,
proof-of-work mining (solo/pool/cloud), token rewards for on-time/late
returns, and two token ledger models (UTXO and account-based).

## Features

**Core chain (Formative 1)**
- Book and member registries (`books.txt`, `members.txt`) loaded into memory
  at startup and validated before any lending action is recorded.
- A genesis-anchored blockchain where every block is signed (ECDSA P-256)
  and hashed (SHA-256, covering every field including the signature).
- Chain validation that recomputes every hash, checks every hash link, and
  verifies every signature.
- A tamper demo that mutates a past block in place (no re-sign) so
  Validate Chain can be shown catching it.

**Pending pool & mining (Formative 2)**
- Borrow/return actions no longer go straight onto the chain - they're
  signed and placed in a pending pool, then confirmed by mining.
- Three mining modes, all operating on the same pending pool:
  - **Solo** - real proof-of-work (nonce search until the hash has N
    leading zero hex characters), adjustable difficulty (1-4).
  - **Pool** - the same real proof-of-work, with the resulting mining
    reward (minus a 2% pool fee) split across simulated miners by a
    randomly weighted share of the total hash attempts.
  - **Cloud** - rents hash power over 1-5 rounds, tracking a fixed
    rental fee against a randomized reward each round, with a running
    net-profit total and a warning if the rental goes unprofitable.
- A confirmed RETURNED block carries a token reward: 10 coins if returned
  within 15 seconds of being borrowed (treated as "on-time" - see note
  below), 5 coins if later, 0 if never returned.

**Token ledgers (Formative 2)**
- **UTXO model** - unspent transaction outputs, inputs/outputs, a 1-coin
  fee per transaction, change outputs, and double-spend prevention (a
  spent output is removed from the set, so it can't be spent again).
- **Account model** - per-member balance and nonce (incremented on every
  outgoing transfer; wrong/reused nonces are rejected), plus a per-account
  transaction history kept as an in-memory linked list.
- Chosen once per session (menu prompt or `--model` flag) and applied to
  all token activity for that run.

**Persistence**
- Chain (`chain.dat`), pending pool (`pending_pool.dat`), UTXO set
  (`utxo_set.dat`), and account balances/nonces (`account_ledger.dat`) all
  survive a restart. Per-account transaction *history* is intentionally
  in-memory only (per the assignment's own description of it as such) and
  starts empty each run.
- The signing keypair is generated once and reused (`keys/private.pem`,
  `keys/public.pem`).

> **Note on the 15-second loan period:** a real loan period would be
> measured in days, but that isn't demoable inside a short video. This is
> a deliberate simulation shortcut, not an oversight - see the technical
> report for the full reasoning.

## Required libraries / dependencies

No new dependencies were added for Formative 2 - it's the same requirements as before:

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

Optional flags (both can be combined, and both can also be set/changed
from inside the menu):

```bash
./library_tracker --difficulty 3          # mining difficulty, 1-4 (default 2)
./library_tracker --model account         # 'utxo' (default) or 'account'
./library_tracker --difficulty 3 --model account
```

On first run the program will:
1. Load `books.txt` and `members.txt` (exits with an error if either is
   missing or empty).
2. Generate an ECDSA P-256 keypair into `keys/` (reused on later runs).
3. Create a genesis block and persist the chain to `chain.dat` (reused on
   later runs), along with any pending pool / ledger state from a
   previous session.
4. Prompt for a transaction model (UTXO or account-based) unless one was
   given via `--model` - this choice is locked for the rest of the session.

### How to switch transaction models

Either pass `--model utxo` / `--model account` on the command line, or
just answer the startup prompt (`1` for UTXO, `2` for account-based, UTXO
is the default on a blank answer). Whichever you pick, that's the model
real reward crediting and mining rewards go through for the whole run -
the menu will refuse the *other* model's "Send Tokens" option with an
error if you try to use it mid-session, though its balances stay viewable.

### How to set the mining difficulty

Either `--difficulty N` at startup, or menu option **"Set Mining
Difficulty"** at any time (1-4, default 2). Higher difficulty means more
leading zero hex characters required in a block's hash, which means more
proof-of-work attempts on average - all three mining menu options use
whatever difficulty is currently set.

### How to test the mining simulations

A quick end-to-end sequence from the main menu:

```
1  Borrow Book        -> enter a valid book_id/member_id
2  Return Book        -> return it (immediately = on-time/10 coins,
                          wait 15+ seconds first = late/5 coins)
3  View Pending Pool   -> see the two unconfirmed blocks waiting
4  Mine (Solo)         -> real proof-of-work, confirms both blocks,
                          credits the return's token reward and the
                          miner's block reward
```
or swap step 4 for **5. Mine (Pool)** (choose how many simulated miners,
see the attempt/share/reward table) or **6. Mine (Cloud)** (choose a
renter ID and 1-5 rounds, see the per-round fee/reward/profit table and
any unprofitability warning).

### Menu

```
--- Core ---
1.  Borrow Book                        3.  View Pending Pool
2.  Return Book                        7.  View Lending Records (confirmed)
                                        8.  Validate Chain
--- Mining ---
4.  Mine Pending Pool (Solo)
5.  Mine Pending Pool (Pool)
6.  Mine Pending Pool (Cloud)
--- UTXO model ---
9.  View Token Balances & UTXO Set     11. Credit Test Reward (dev/test hook)
10. Send Tokens (UTXO Transfer)
--- Account model ---
12. View Account Balances              14. View Transaction History
13. Send Tokens (Account Transfer)     15. Credit Test Reward (dev/test hook)
--- System ---
16. Set Mining Difficulty              18. Exit
17. Tamper With a Past Block (Demo)
```

"Credit Test Reward" under each model is a manual utility for setting up
balances to test edge cases (insufficient balance, bad nonce) without
waiting on a real borrow/return/mine cycle - the real reward path is
fully automatic via Return Book -> Mine.

## File layout

```
src/
  main.c            CLI menu loop, model selection, reward determination
  registry.c/h      Book/Member structs, file loaders, lookups
  crypto.c/h        SHA-256, ECDSA sign/verify, transaction-id hashing (OpenSSL)
  blockchain.c/h    Block struct, hashing, signing, chain validation
  pending_pool.c/h  Unconfirmed blocks awaiting mining
  mining.c/h        Proof-of-work, solo/pool/cloud mining simulations
  utxo.c/h          UTXO set, mint/transfer, fees, change outputs
  account.c/h       Per-member balances, nonces, linked-list history
  storage.c/h       Binary persistence for all of the above
books.txt           Sample book registry
members.txt         Sample member registry
keys/               Generated ECDSA keypair (gitignored; created at first run)
docs/               Formative 2 Technical Report (.md source + .docx)
```

## Resetting to a clean state

```bash
rm -f chain.dat pending_pool.dat utxo_set.dat account_ledger.dat \
      keys/private.pem keys/public.pem
```

This wipes the persisted chain, pending pool, both ledgers, and the
signing key so the next run starts completely fresh.
