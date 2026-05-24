# Indexes

Indexes are the application-defined committed state stores. The library
calls them "indexes" rather than "tables" or "state" because everything in
the chain that survives a block commit lives behind an index: balances,
account lookups, time-window scans, vector search, graph edges, anything.
The library never assumes a particular shape for any of them.

## Interface

There is a single base class, `BlockchainIndex`. There is no non-segmented
index in this library: every index is segment-based by definition, and the
library only orchestrates segment-based indexes.

```cpp
class BlockchainIndex {
public:
    virtual ~BlockchainIndex() = default;
    virtual uint16_t encodingVersion() const = 0;
    virtual Bytestring writeSegment(uint64_t blockRangeStart, uint64_t blockRangeEnd) const = 0;
    virtual bool readSegment(const Bytestring& payload, uint16_t encodingVersionOnDisk,
                              uint64_t blockRangeStart, uint64_t blockRangeEnd) = 0;
    virtual Bytestring mergeSegments(const ArrayList<Bytestring>& payloads,
                                      const ArrayList<uint16_t>& encodingVersionsOnDisk) const = 0;
};
```

## Registering indexes

Inside `ChainDesign::registerIndexes(BackendRegistry&)`:

```cpp
void MyChainDesign::registerIndexes(BackendRegistry& registry) override {
    registry.registerIndex<BalanceIndex>(sp<BalanceIndex>::create());
    registry.registerIndex<BalanceIndex>(sp<BalanceIndex>::create());   // id=1
    registry.registerIndex<VectorIndex>(sp<VectorIndex>::create());     // id=0
}
```

Each call adds a new instance under the family for type `T`. Ids are
allocated in registration order, starting at `0`, per type. The same `T`
can be registered multiple times for parallel instances (for example a
chain that runs two separate balance tables for two coins).

Access from the backend:

```cpp
sp<BalanceIndex> coin0 = backend.index<BalanceIndex>(0);
sp<BalanceIndex> coin1 = backend.index<BalanceIndex>(1);
sp<VectorIndex>  vec   = backend.index<VectorIndex>(0);
```

`backend.index<T>(id)` returns a `sp<T>` directly with no casts. If
`(T, id)` is not registered, it returns a null `sp<T>`.

`backend.instanceCount<T>()` (via the underlying registry) returns the
number of registered instances of `T`.

## The typed registry, in one paragraph

`BackendRegistry` stores indexes in typed families: `HashMap<TypeKey,
sp<IndexFamilyBase>>` where each `IndexFamilyBase` is actually an
`IndexFamily<T>` holding `ArrayList<sp<T>>`. Type erasure is confined to
the family boundary; the cast back to `IndexFamily<T>` happens inside
`getIndex<T>()` and not in application code. The `TypeKey` for `T` is
derived from a function-local static address (see
`src/blockchain/TypeKey.h`) so it is process-stable, RTTI-free, and has no
initialization-order hazards.

The matching machinery for override families is `StateOverrideRegistry`
(see [state overrides](state-overrides.md)).

## Reads in Phase 1

Indexes can be queried directly from the backend as soon as
`registerIndexes` has populated them. There is no required read API on
`Index` itself; what queries look like is part of the application's index
implementation. Typical patterns:

```cpp
backend.index<BalanceIndex>(0)->balanceOf(account, blockHeight);
backend.index<VectorIndex>(0)->nearest(queryVector, k, blockHeight);
```

The library does not constrain method names, return types, or block-height
semantics. In Phase 2+ the catalog will let indexes filter by block-range,
but applications are free to surface block-height-aware reads today.

## Writes in Phase 1

In the final design, an index never writes itself directly. Instead, at
block commit time the backend asks each registered override family to
`seal()` its pending changes into a `Bytestring`, then hands those bytes
to the matching index as a new segment.

In Phase 1 there is no segment-storage layer yet, so writes are stored in
whatever in-memory or ad-hoc structure the override family chooses, and
committed by the override family's own internal state being preserved
across block-commit boundaries.

In practice the simplest Phase 1 index pairs a "delta" override family
(which a transaction writes into via `s.override<...>(id)`) with a
counterpart object that holds the committed-side data. The override
family's `seal()` is a no-op for now.

## What's reserved for Phase 2+

These are designed but not implemented:

- **Catalog**: per-catalog-file BTree of segments keyed by `(blockRangeStart,
  blockRangeEnd)`, indexed by index type and instance id; top-level
  table-of-contents BTree mapping block-range queries to catalog files; a
  256-bit bloom-style bitmask per catalog file filtering by `(indexTypeKey,
  instanceId)`.
- **ContainerManager**: groups segments into shared physical containers
  under a sub-2 GiB size threshold using `FreeSpaceFile` for intra-
  container region management.
- **Segment lifecycle orchestration**: the backend drives `writeSegment`,
  `readSegment`, and `mergeSegments` on registered `BlockchainIndex`
  subclasses; in Phase 1 these methods exist on the interface but are not
  yet called by the library.
- **Compaction policy**: smallest-first merges, threshold-triggered, run on
  libexcessive's `ThreadPool`, never blocking the block builder.
- **encodingVersion rejection**: if `encodingVersion()` does not match a
  segment header, the affected block range is reindexed from the journal.
  Indexes do not need to support every prior encoding.
- **TimeIndex**: a library-provided `BlockchainIndex` for time-window
  queries (mempool tail merged in by the frontend).
- **Crash recovery**: indexes are not `fsync`'d. On a crash, any
  index-degraded blocks are detected on startup and reindexed.

When these land, `BlockchainIndex`'s existing segment lifecycle methods
will start being called by the backend, but `BackendRegistry::registerIndex<T>`
and `backend.index<T>(id)` will not change.
