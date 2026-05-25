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
    virtual void attach(Catalog* catalog, IndexContainerManager* containers,
                        uint16_t persistentTypeId, uint8_t instanceId);
    virtual Bytestring mergeSegments(const ArrayList<SegmentLocator>& inputs) const = 0;
};
```

An index is a lightweight query handle. It does not hold committed
state in RAM. At query time it asks the attached `Catalog` which
segments cover the block range of interest and mmaps the matching
payloads through the attached `IndexContainerManager`. Most queries
only touch a fragment of one or two segments, so the working set stays
bounded regardless of chain size.

Segment payload bytes are produced at commit time by the corresponding
`IndexOverrideFamilyBase::seal()` (see
[state-overrides.md](state-overrides.md)). The index itself never
writes a payload directly; it only reads them back and merges them.

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

## Reads

Indexes can be queried directly from the backend as soon as
`registerIndexes` has populated them. There is no required read API on
`BlockchainIndex` itself; what queries look like is part of the
application's index implementation. Typical patterns:

```cpp
backend.index<BalanceIndex>(0)->balanceOf(account, blockHeight);
backend.index<VectorIndex>(0)->nearest(queryVector, k, blockHeight);
```

The library does not constrain method names, return types, or block-height
semantics.

## Writes: the segment lifecycle

An index never writes a segment directly. At block commit time the
backend:

1. Asks each registered override family to `seal()` its pending changes
   into a `Bytestring`.
2. Writes that payload through `IndexContainerManager` to the matching
   index's container file and records a `SegmentLocator` in the
   `Catalog`.
3. Opportunistically merges segments via `mergeSegments(inputs)` when
   the segment count for an index exceeds
   `BlockchainConfig::maxSegmentsPerIndex` and at least two candidates
   are under `BlockchainConfig::maxMergeableSegmentBytes`. After a
   successful merge the input segments are removed from the catalog and
   their disk regions are freed back to the container's `FreeSpaceFile`.

## Reading segments

Indexes mmap segments lazily through the catalog and container
manager that were wired in by `attach()`. A typical query looks like:

```cpp
ArrayList<SegmentLocator> segs = attachedCatalog->rangeScan(
    attachedPersistentTypeId, attachedInstanceId, fromBlock, toBlock);
for (int i = 0; i < segs.size(); i++) {
    const SegmentLocator& loc = segs.get(i);
    IndexContainer* c = attachedContainers->get(
        loc.persistentTypeId, loc.instanceId, loc.containerId);
    IndexContainer::PayloadView view = c->mmapPayload(loc.byteOffset, loc.byteLength);
    // interpret view.data[0 .. view.length) according to loc.encodingVersion
}
```

Because `rangeScan` already returns segments in ascending
`(blockRangeStart, mergeGeneration)` order, the index can fold delta
segments forward or short-circuit on the first absolute snapshot it
finds, depending on its encoding.

## `mergeSegments`

When the backend triggers a merge it hands the index a list of
`SegmentLocator`s in ascending `(blockRangeStart, mergeGeneration)`
order. The index is responsible for mmap'ing each input via its
attached container manager, computing the combined payload covering
the union of all input block ranges, and returning that as a single
`Bytestring`. Returning an empty `Bytestring` aborts the merge.

## Storage layout on disk

A backend rooted at `dataDir/` lays out its files like this:

```
dataDir/
    metadata.bin              chain height + last block timestamp
    epochs/                   fsync'd transaction journal (source of truth)
        0.bin
        1.bin
        ...
    catalog/
        toc.bin               table-of-contents: per catalog-file metadata
                              (block range, segment count, payload size,
                              256-bit bloom over (typeId, instanceId))
        files/
            <fileId>.bin      sorted log of SegmentLocator records; one
                              file per ~2 GiB of catalogued payload
    indexes/
        <containerId>.bin     packed segment containers; each file may
                              hold payloads from many different indexes
                              side-by-side, capped near 2 GiB
```

The catalog and the index containers are **not** `fsync`'d. The journal
is the source of truth; if the catalog or any container is corrupted on
restart, the affected block range can be reindexed from the journal.
This trade keeps the commit fast path I/O-bound only on the journal.

`IndexContainer` is backed by libexcessive's `FreeSpaceFile`, which
lets compacted-away segment regions be reclaimed without rewriting the
whole file. When compaction removes a segment from the catalog the
backend calls `freeRegion(offset, length)` on the owning container so
future segment writes can reuse the space.

## Remaining work

- **TimeIndex**: a library-provided `BlockchainIndex` subclass for
  time-window queries (with the frontend's mempool tail merged in). For
  now `BlockchainBackend::getTransactionsByTimeWindow` still scans the
  journal directly.
- **Crash recovery / index-degraded blocks**: detect-and-reindex on
  startup for individual block ranges whose container payloads fail
  checksum.
- **Parallel compaction**: today merges run inline on the commit thread.
  Future work moves them onto libexcessive's `ThreadPool` so the
  commit thread never blocks on a merge.
