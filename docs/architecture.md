# Architecture overview (Phase 1)

LibMagelessChain is split into a small set of well-defined pieces. This page
gives the shape of each one and shows how data flows through them. For
deeper detail on any piece, follow the link.

## Pieces

- **`ChainDesign`** (virtual, application supplied). The single factory the
  library uses to learn the shape of your chain. It has three registration
  hooks: indexes, override families, and transaction types. The design
  object lives alongside the backend for the lifetime of the chain. See
  [chain-design.md](chain-design.md).

- **`Transaction`** (virtual, application supplied). Subclasses define their
  payload, serialization, validity, value scoring, and `apply` logic. The
  library never inspects transaction contents; it only ever asks the
  transaction to read or write itself through a `StateOverride`. See
  [transactions.md](transactions.md).

- **`BlockchainIndex`** (virtual, application supplied).
  Application-defined committed state stores. In Phase 1 only the
  interfaces are wired; the storage orchestration (catalog, container
  manager, segment lifecycle) is **(Phase 2+)**. See [indexes.md](indexes.md).

- **`IndexOverrideFamilyBase`** (virtual, application supplied). The
  per-index pending-changes layer. One override family pairs with one
  registered index instance and provides typed read/write access to its
  pending modifications. See [state-overrides.md](state-overrides.md).

- **`StateOverride`** (concrete, library owned). A typed bundle of override
  families representing one block-in-progress worth of pending changes.
  Replaces the old cumulative-state `BlockchainStateSnapshot`. Forkable via
  `sp<T>` copy-on-write. See [state-overrides.md](state-overrides.md).

- **`BackendRegistry`** and **`StateOverrideRegistry`** (concrete, library
  owned). Typed, per-backend registries populated by `ChainDesign`. Indexes
  live in `BackendRegistry`; override-family slots live in
  `StateOverrideRegistry`. Both expose typed access via a small template
  surface so no application code ever casts. See [indexes.md](indexes.md).

- **`TransactionTypeRegistry`** (concrete, library owned, per-backend).
  Maps a 1-byte type id to a factory that deserializes the corresponding
  `Transaction` subclass from the journal or wire. See
  [transactions.md](transactions.md).

- **`MEVBuilder`** (concrete, library owned). Iteratively scores transactions
  via `Transaction::computeValue` against a forked `StateOverride` and picks
  an ordered block. See [mev-builder.md](mev-builder.md).

- **`BlockchainBackend`** (concrete, library owned). Owns the durable epoch
  journal, the three typed registries, and the interim committed state.
  Exposes `index<T>(id)`, `newStateOverride()`, `getBlock`,
  `getTransactionsByTimeWindow`. See [backend-and-frontend.md](backend-and-frontend.md).

- **`BlockchainFrontend`** (concrete, library owned). Owns the mempool, the
  speculative `StateOverride`, and the background block-builder thread.
  The only filesystem object it owns is `mempool.bin`. See
  [backend-and-frontend.md](backend-and-frontend.md).

## How a block flows

```
sendTransaction(tx)
        |
        v
+---------------------+    apply(state)
|     Frontend        |--------------------+
| mempool, spec state |                    |
+---------------------+                    v
        |                          speculative StateOverride
        | (builder thread periodically calls)
        v
+---------------------+    fork via sp<T> CoW
|     MEVBuilder      |-----------------------------+
| iteratively scores  |                             |
| computeValue, picks |                             v
+---------------------+                  candidate StateOverride
        |
        | ordered ArrayList<sp<Transaction>>
        v
+---------------------+
|     Backend         |
| addBlock(txs):      |
|  1. write journal   |  <-- only thing that is fsync'd
|  2. apply to        |
|     committedInterim|
|  3. (Phase 2+) seal |
|     override        |
|     families into   |
|     segments        |
+---------------------+
```

After commit:

- The backend's `committedInterim` reflects the new committed state.
- The frontend rebuilds its speculative state by calling
  `backend.newStateOverride()` and reapplying every still-pending mempool
  transaction in order.

## What Phase 1 includes

- Typed registries (`BackendRegistry`, `StateOverrideRegistry`,
  `TransactionTypeRegistry`) populated by `ChainDesign`.
- `StateOverride` with typed `override<T>(id)` access and `sp<T>` CoW
  forking.
- `Transaction::apply(StateOverride&)` semantics, `computeValue`,
  per-backend type registry.
- Backend that exposes `index<T>(id)`, `newStateOverride()`, block and
  time-window queries, and uses the legacy epoch journal for persistence.
- Frontend that owns the mempool, the speculative `StateOverride`, and the
  block-builder thread, with mempool persistence to `mempool.bin`.
- MEVBuilder that forks the override and iteratively re-scores transactions
  so state-dependent value is respected.
- All previous tests adapted to the new shape; 20/20 pass.

## What Phase 1 defers **(Phase 2+)**

These are scoped for follow-up sessions and are unimplemented today:

- Real segment-backed index storage:
  - **Catalog** with per-catalog-file BTree, top-level table-of-contents
    BTree, and per-catalog 256-bit bloom bitmask.
  - **ContainerManager** with `FreeSpaceFile`-backed sub-2 GiB packing.
  - **Segment lifecycle** through `BlockchainIndex` (write, read, merge,
    encodingVersion rejection).
- Compaction policy and `ThreadPool` scheduling for merges and catalog
  writes.
- `TimeIndex`, a library-provided tree-segmented index for time-window
  queries.
- Crash recovery and "index-degraded" block state with reindex retries.
- The full suite of new test files for indexing, catalog, container
  manager, segment orchestration, compaction, crash recovery, and MEV
  forking correctness.

## Phase 1 stopgaps you should know about

These are *temporary* and will be removed when Phase 2+ lands. The public
API does not depend on them.

- `BlockchainBackend::committedInterim`: a `sp<StateOverride>` rebuilt by
  replaying the journal at startup. Once indexes are segment-backed,
  `newStateOverride()` will produce an empty override that reads through
  the indexes directly, and replay will be gone.
- The `replayJournalIntoCommittedOverride` method is the only thing that
  replays history. Application code must never depend on its presence.
- `IndexOverrideFamilyBase::seal()` is allowed to return an empty
  `Bytestring` and is otherwise unused in Phase 1.
- The legacy `BlockchainStateSnapshot` type is gone. If you see references
  to it in any external code or notes, treat them as obsolete.
