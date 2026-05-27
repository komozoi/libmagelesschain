# LibMagelessChain

LibMagelessChain is a C++ library for building specialized blockchain databases. Unlike general-purpose public
blockchains, it targets databases that may be private, semi-private, or public, with highly customizable structure
and on-disk layout. The library is the finalization layer, journal, validation mechanism, consensus mechanism, and
indexed storage engine for a blockchain. It also defines the wire format and orchestrates serialization of blocks,
transactions, and sync messages, so all nodes built on it agree on how data is encoded over the wire.

What the library is **not** is a network stack. It does not open sockets, run a P2P discovery protocol, or speak any
particular transport. Given an `FdHandle` to read from or write to, or a buffer of bytes, it can encode and decode
the protocol's messages. Whether those bytes travel over TCP, QUIC, a Unix socket, a serial port, a pigeon, or are
stuffed into a database column is entirely the application's concern.

The library orchestrates on-disk storage, segment management, indexing, block building and MEV optimization,
consensus mechanics, and the wire format. It does **not** define the chain's data model: the application provides
that via a `ChainDesign` object that registers transaction types, indexes, and state-override families. Any project
built on LibMagelessChain must supply:

- A transport layer that moves bytes between nodes (TCP, QUIC, Unix sockets, serial, whatever). The library will
  read from and write to any `FdHandle` or byte buffer you hand it.
- A peer discovery and connection management strategy.
- A `ChainDesign` implementation describing the chain's transactions, indexes, and override families.
- Transaction `apply` logic: what a transaction reads, what it writes, and what counts as success.
- Application-defined index implementations (segment encoding, queries, merging). The library handles segment
  framing, container packing, the catalog, and compaction scheduling.
- An MEV objective via `Transaction::computeValue`. The library handles candidate exploration and ordering.

## Try it in 5 minutes

Create a `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyChain)

include(FetchContent)

# Add libmagelesschain to the project
FetchContent_Declare(
    libmagelesschain
    GIT_REPOSITORY https://gitea.com/komozoi/libmagelesschain.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libmagelesschain)

add_executable(mychain main.cpp)
target_link_libraries(mychain PRIVATE LibMagelessChain)
```

And create a file `main.cpp`:

```cpp
#include <blockchain/BlockchainFrontend.h>
#include <blockchain/BlockchainBackend.h>
#include <blockchain/Transaction.h>
#include <blockchain/ChainDesign.h>
#include <blockchain/IndexOverride.h>
#include <Logger.h>

// 1. Define your State (as an Override Family)
class MyOverride : public IndexOverrideFamilyBase {
public:
	int counter = 0;
};

// 2. Define your Transaction
class IncrementTx : public Transaction {
public:
	bool verify(const StateOverride& state) const override { return true; }
	
	bool apply(StateOverride& state) const override {
		// Access the custom state family by id (registration order)
		state.override<MyOverride>(0).counter++;
		return true;
	}
	
	float computeValue(const StateOverride& state) const override { return 1.0f; }
	
	void write(MmapHandle* dst) const override { 
		dst->write(getTypeId());
	}
	
	size_t size() const override { return sizeof(uint8_t); }
	uint8_t getTypeId() const override { return 1; }
	uint64_t getTimestamp() const override { return 0; }
	
	static sp<Transaction> createFromMmap(MmapHandle* src) { 
		return sp<IncrementTx>::create(); 
	}
};

// 3. Define your Chain Design
class MyChainDesign : public ChainDesign {
public:
	void registerIndexes(BackendRegistry& registry) override {
		// Basic example: no persistent indexes yet
	}
	void registerOverrides(StateOverrideRegistry& registry) override {
		registry.registerOverride<MyOverride>();
	}
	void registerTransactionTypes(TransactionTypeRegistry& registry) override {
		registry.registerType(1, &IncrementTx::createFromMmap);
	}
};

int main() {
	// Initialize logger
	Logger logger("logs", 0, 0);
	
	// Create the chain design
	sp<MyChainDesign> design = sp<MyChainDesign>::create();

	// Initialize backend and frontend
	BlockchainBackend backend(logger, "data", design);
	BlockchainFrontend frontend(backend);

	// Send a transaction to the mempool
	frontend.sendTransaction(sp<IncrementTx>::create());
	
	// The state in the frontend includes the effects of transactions in the mempool
	LogEndpoint log(logger, "Main");
	log.info("Current counter: %d", frontend.getState()->override<MyOverride>(0).counter);

	return 0;
}
```

For more detailed examples, including persistent indexing and MEV strategies, see [docs/examples.md](docs/examples.md).

## Key Features

* **Application-defined chain model via `ChainDesign`.** A single virtual interface where the application registers
  its transaction types, indexes, and state-override families. The library uses it as a factory throughout the
  backend's lifetime; there is no hard-coded chain shape.
* **Index-centric storage.** Committed state lives in application-defined indexes, not in an in-RAM snapshot.
  Indexes can be anything: key/value tables, multidimensional arrays, vector search, graph stores, etc. The library
  never assumes a particular state structure and never holds the chain state in memory.
* **Library-orchestrated segments.** Indexes define what goes into
  each segment; the library decides *where* segments live, wraps them
  with checksums, and tracks them through a catalog.
* **Catalog with table-of-contents.** Each catalog file holds a BTree of segments keyed by block range; a separate
  table-of-contents BTree maps block-range queries to catalog files. A 256-bit bloom-style bitmask per catalog file
  filters by index type and instance ID so range scans only touch relevant catalogs.
* **Typed registries with no casts in application code.** Indexes are accessed by `backend.index<T>(id)`; override
  families are accessed by `s.override<T>(id)`. Both use type-key machinery internally so the application never
  needs `static_cast`, `dynamic_cast`, or string lookups to reach its own data.
* **State overrides instead of cumulative state.** A `StateOverride` is a typed bundle of pending modifications
  layered on top of the committed indexes. The frontend keeps one to reflect the mempool; MEV candidates fork it
  cheaply via `sp<T>` copy-on-write; at commit, the backend builds a fresh override and seals each family into one
  segment per index instance.
* **No transaction replay on startup.** State is reconstructable from indexes, not from re-running history. Only the
  mempool, which is bounded by mempool size rather than chain size, is replayed at frontend startup.
* **MEV-aware block builder.** `MEVBuilder` iteratively explores candidate orderings, scoring transactions through
  `computeValue` against a forked override so that state-dependent value is evaluated correctly. "Value" is whatever
  the application optimizes for: fee revenue, data completeness, transaction success rate, ordering fairness, etc.
* **Built-in `TimeIndex` (Planned).** A tree-segment-based index that handles time-window queries
  (mempool tail merged in by the frontend). Currently time-window queries scan the journal via a block-timestamp
  tracker; the migration to a segmented index will happen in a future phase.
* **Segment compaction (Planned).** Smallest segments are merged in the background via `libexcessive`'s `ThreadPool` once the
  configured segment-count threshold is crossed and at least two candidates are below 500 MiB. Indexes implement
  `mergeSegments`; the library schedules and orchestrates.
* **Crash-tolerant by construction.** Only the epoch journal is `fsync`'d; it is the source of truth. Indexes,
  catalogs, and containers are never `fsync`'d because they are fully reconstructable from the journal.
  Index-degraded blocks are detected, retried, and surfaced via a status accessor.
* **Customizable transaction semantics.** Transactions are defined entirely by the application: serialization,
  validation, `apply` logic, value scoring, and timestamp. The library handles persistence, ordering, ID assignment
  (44-bit block number + 20-bit transaction index), and time-window queries.

## Architecture

LibMagelessChain keeps a strict split between the **Frontend** (mempool, speculative state, block builder) and the
**Backend** (committed journal, indexes, catalog).

### Backend

- Owns the **epoch journal**: the durable, `fsync`'d record of every committed block. This is the only file the
  library considers irrecoverable on loss.
- Owns the **index registry**: one `IndexFamily<T>` per registered index type, each holding a dense `ArrayList<sp<T>>`
  of instances by registration order.
- Owns the **catalog**: per-catalog-file BTrees over `(blockRangeStart, blockRangeEnd) → {indexId,
  byteOffset, byteLength, encodingVersion, checksum, mergeGeneration}`,
  plus a top-level table-of-contents BTree and a 256-bit bloom bitmask
  per catalog file.
- Owns the **ThreadPool** used for segment merges and catalog writes. Block building is *not* on the pool; it lives
  on a dedicated frontend thread.
- Provides `backend.index<T>(id)` for typed query access and `backend.newStateOverride()` for fresh overrides.
- Does **not** hold a `BlockchainStateSnapshot` or any cumulative in-RAM state; that type is removed.

### Frontend

- Owns the **mempool** (in-memory + on-disk persistence file).
- Owns the **current `StateOverride`**, a single `sp<StateOverride>` reflecting the speculative effect of every
  mempool transaction on top of the committed chain. Updated immediately on `sendTransaction`, rebuilt fresh after
  every block commit.
- Runs a **persistent block-builder thread** that periodically asks `MEVBuilder` for an ordered block and submits it
  to the backend.
- Replays the **mempool only** at startup (bounded work).
- Never opens index, segment, or catalog files. Its only filesystem responsibility is the mempool file.

### Indexes and Segments

- Each application index inherits from `BlockchainIndex` and implements `mergeSegments` and `encodingVersion`.
- Segment payload *contents* are application-owned and may use any encoding the index chooses.
- Segment *framing* (location, checksum, length, catalog entry) is library-owned.
- One segment per `(index instance, block)` pair is produced at commit when the corresponding override family is
  non-empty. Segments are never fragmented.
- Each index instance's segments live in its own logical lineage but may share physical catalog files with other indexes.
- Encoding versions are per-segment, not per-index. An index that cannot decode an older segment causes that segment
  to be discarded and the affected block range to be rebuilt from the journal.

### State Overrides

- `StateOverride` is a concrete library type containing the registered override families.
- Override families are application-defined, accessed typed via `s.override<T>(id)`.
- A transaction's `apply(StateOverride&)` reads through the override (which layers pending changes over the
  committed indexes) and writes through typed override-family APIs.
- Forks use `sp<T>`'s built-in CoW: untouched override families are shared across forks, and mutations clone only
  the touched family.
- Failed transactions (returning `false` from `apply`) are still committed to the block and remain queryable, but
  contribute no state delta.

### Block Commit Path

1. Frontend builds a candidate block via `MEVBuilder` over a forked override.
2. Frontend hands the ordered transactions to the backend.
3. Backend writes the block to the journal and `fsync`s.
4. Backend builds a fresh override and applies the block's transactions in order.
5. Backend seals each registered index's override family into a segment payload, writes it to the catalog, and
   inserts a catalog entry.
6. On any indexing failure, the block is marked **index-degraded** and an immediate reindex is attempted; if that
   fails, the block remains readable from the journal.

## Dependencies

- [**libexcessive**](https://github.com/komozoi/libexcessive): Core utilities. `sp<T>` (with CoW), `ArrayList`,
  `HashMap`, `BTree`, `Bytestring`, `FdHandle`, `FreeSpaceFile`, `ThreadPool`, `Logger`, `millis_since_epoch()`,
  hash functions used for segment checksums, and other primitives the library standardizes on instead of the
  C++ stdlib equivalents.
- **GoogleTest** (optional): test suite only.

No other runtime dependencies. No `curl`, no networking stack, no JSON library, no STL containers in hot paths.

## Building

LibMagelessChain uses CMake:

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug --target LibMagelessChain
```

To build and run the test suite:

```bash
cmake --build cmake-build-debug --target mageless_tests
./cmake-build-debug/mageless_tests
```

Test artifacts (data and logs) are written under `cmake-build-debug/test_data` and `cmake-build-debug/test_logs`.

## License

This project is licensed under the Apache License, Version 2.0. See the source file headers for details.
