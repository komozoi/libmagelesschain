# Architecture overview

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
  Application-defined committed state stores. The library orchestrates
  the full segment lifecycle: write, read, merge, and threshold-driven
  compaction. See [indexes.md](indexes.md).

- **`IndexOverrideFamilyBase`** (virtual, application supplied). The
  per-index pending-changes layer. One override family pairs with one
  registered index instance and provides typed read/write access to its
  pending modifications. See [state-overrides.md](state-overrides.md).

- **`StateOverride`** (concrete, library owned). A typed bundle of override
  families representing one block-in-progress worth of pending changes.
  Forkable via `sp<T>` copy-on-write. See [state-overrides.md](state-overrides.md).

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

- **`BlockchainBackend`** (concrete, library owned). Owns the durable
  epoch journal, the typed registries, the catalog, and the index
  container manager. Exposes `index<T>(id)`, `newStateOverride()`,
  `getBlock`, `getTransactionsByTimeWindow`. See
  [backend-and-frontend.md](backend-and-frontend.md).

- **`Catalog`** (concrete, library owned). Records every segment's
  location and metadata. Range-scans by `(persistentTypeId, instanceId,
  blockRange)` return segments in ascending
  `(blockRangeStart, mergeGeneration)` order. Replayed from a per-chain
  append-only log on startup. See [indexes.md](indexes.md).

- **`IndexContainerManager`** + **`IndexContainer`** (concrete, library
  owned). Owns the per-index payload files under `dataDir/indexes/` and
  hands out byte regions via libexcessive's `FreeSpaceFile`. Freed
  regions (post-compaction) are reclaimed for future segment writes.

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
|  2. seal override   |
|     families ->     |
|     container write |
|     + catalog insert|
|  3. compact if over |
|     maxSegments     |
+---------------------+
```

After commit:

- The new committed segments are visible to any query through the
  catalog; indexes read them lazily via mmap.
- The frontend rebuilds its speculative state by calling
  `backend.newStateOverride()` and reapplying every still-pending mempool
  transaction in order.

## What's implemented today

- Typed registries (`BackendRegistry`, `StateOverrideRegistry`,
  `TransactionTypeRegistry`) populated by `ChainDesign`.
- `StateOverride` with typed `override<T>(id)` access and `sp<T>` CoW
  forking.
- `Transaction::apply(StateOverride&)` semantics, `computeValue`,
  per-backend type registry.
- Backend that exposes `index<T>(id)`, `newStateOverride()`, block and
  time-window queries, and persists transactions to the fsync'd epoch
  journal.
- Frontend that owns the mempool, the speculative `StateOverride`, and the
  block-builder thread, with mempool persistence to `mempool.bin`.
- MEVBuilder that forks the override and iteratively re-scores transactions
  so state-dependent value is respected.
- Segment-backed indexes: at commit time the backend asks each override
  family to `seal()` a payload, writes it through `IndexContainerManager`,
  and records a `SegmentLocator` in the `Catalog`. Indexes themselves
  hold no committed state in RAM; they query the catalog and mmap the
  segments they need at read time.
- Threshold-driven compaction via `BlockchainIndex::mergeSegments`. The
  backend writes the merged output, catalogs it, then removes the input
  segments from the catalog and frees their disk regions in the owning
  container. There is no "obsolete" state: a segment either exists in
  the catalog or it does not.
- On reopen the backend only attaches each registered index to the
  catalog and container manager. Queries read segments lazily, so no
  transaction replay and no startup scan are required.

## Stopgaps still to address

These are unfinished work, not contracts; the public API will not change.

- Compaction runs inline on the commit thread; libexcessive's
  `ThreadPool` will take over once concurrency hazards are scoped.
- `TimeIndex` (a library-provided `BlockchainIndex` for time-window
  queries) is not yet implemented; `getTransactionsByTimeWindow` still
  scans the journal directly.
- Crash-time "index-degraded" block recovery (detect a bad segment on
  startup and reindex from the journal) is logged on reject but not yet
  acted on.
