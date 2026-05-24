# Backend and frontend

LibMagelessChain exposes two top-level handles: `BlockchainBackend` and
`BlockchainFrontend`. The backend is the durable journal and the typed
registry holder. The frontend is the application-facing object that owns
the mempool, the speculative `StateOverride`, and the background block-
builder thread.

## BlockchainConfig

Both classes accept a `BlockchainConfig`:

```cpp
struct BlockchainConfig {
    uint32_t targetBlockTimeMs = 60000;  // 60s
    uint32_t targetThroughput  = 180;    // transactions per block
};
```

The frontend uses these to drive the block-builder loop's cadence and the
`maxTransactions` argument it passes to `MEVBuilder::buildBlock`. The
backend uses them to enforce minimum block spacing in `addBlock`.

## BlockchainBackend

### Construction

```cpp
sp<ChainDesign> design(sp<MyChainDesign>::create());
BlockchainBackend backend(logger, "/data/mychain", design);
```

During construction the backend:

1. Opens (and creates if needed) the metadata file and the epoch journal.
2. Calls `design->registerIndexes(BackendRegistry&)`.
3. Calls `design->registerOverrides(StateOverrideRegistry&)`.
4. Calls `design->registerTransactionTypes(TransactionTypeRegistry&)`.
5. **(Phase 1 stopgap)** Replays the journal into an internal
   `committedInterim: sp<StateOverride>`, which is what
   `newStateOverride()` deep-copies.

The backend is non-copyable and non-movable. The `sp<ChainDesign>` is held
for the backend's lifetime.

### Public API

```cpp
long addBlock(const ArrayList<sp<Transaction>>& transactions);
ArrayList<sp<Transaction>> getBlock(uint64_t blockNumber);
ArrayList<sp<Transaction>> getTransactionsByTimeWindow(uint64_t startMillis,
                                                       uint64_t endMillis);

long getBlockHeight() const;
uint64_t getLastBlockTimestamp() const;
uint64_t getLastBlockTime() const;

sp<StateOverride> newStateOverride() const;

template<typename T> sp<T> index(uint8_t id) const;

const TransactionTypeRegistry& getTransactionTypeRegistry() const;
const StateOverrideRegistry&   getStateOverrideRegistry()   const;
```

#### `addBlock`

Writes the block (header + transactions) to the durable journal, updates
the metadata header, applies each transaction to `committedInterim`, and
returns the new block height. Returns `-1` if called before the
`targetBlockTimeMs` minimum has elapsed since the last commit. Block
numbers are 0-based; block *height* is the count of committed blocks. The
first block is block `0` and after committing it the height is `1`.

#### `getBlock` / `getTransactionsByTimeWindow`

Read-only journal queries. `getBlock` throws `std::range_error` for ids
that have not been committed. `getTransactionsByTimeWindow` returns the
union of committed blocks whose block timestamp falls in `[start, end]`.

#### `newStateOverride`

Returns a freshly forked `sp<StateOverride>` that the caller may freely
mutate without affecting the backend's internal state. In Phase 1 this is
a deep copy of `committedInterim`; in **(Phase 2+)** it will be an empty
override that reads through committed segment-backed indexes.

#### `index<T>(id)`

Typed access to a registered `Index` instance. Returns null `sp<T>` if no
instance of type `T` with that `id` was registered by `ChainDesign`.

### Internal layout

- `metadataFile`: small fixed-size file holding the chain's header
  (block height, last block time, etc).
- `openEpochs`: `HashMap<uint32_t, sp<MmapHandle>>` caching open epoch
  files indexed by epoch id.
- `committedInterim`: stopgap `sp<StateOverride>` reconstructed from the
  journal at startup; will be removed when indexes go segment-backed.

The backend never spawns threads. All persistent threading is owned by the
frontend.

## BlockchainFrontend

### Construction

```cpp
BlockchainFrontend frontend(backend, config);
```

During construction the frontend:

1. Builds the speculative `state` by calling `backend.newStateOverride()`.
2. Loads `mempool.bin` if present and applies each persisted transaction
   to `state`.
3. Starts a persistent background block-builder thread.

The frontend is non-copyable and non-movable. There should only be one
per backend.

### Public API

```cpp
int getBlockHeight() const;
int getMempoolSize() const;

void sendTransaction(const sp<Transaction>& transaction);

sp<Transaction>           getTransactionById(uint64_t id) const;
ArrayList<sp<Transaction>> getTransactionsByTimeWindow(uint64_t startMillis,
                                                       uint64_t endMillis);

sp<StateOverride> getState() const;
```

#### `sendTransaction`

Under the mempool mutex, in order:

1. Append the transaction to the mempool `ArrayList`.
2. Apply the transaction to `state.mut()` so subsequent reads through
   `getState()` reflect the pending change. Failures from `apply` are
   ignored at this stage; the transaction will still be considered by
   `MEVBuilder` and committed to the journal (but contribute no state
   delta on commit, per `Transaction` semantics).
3. Persist the mempool to `mempool.bin`.

#### Time-window query with mempool tail

`frontend.getTransactionsByTimeWindow(start, end)` returns:

- All committed transactions from `backend.getTransactionsByTimeWindow(start, end)`.
- Plus mempool transactions whose own `getTimestamp()` falls in
  `[start, end]`.

This is the canonical entry point applications should use; the backend's
equivalent does not include the mempool tail.

#### `getState`

Returns the speculative `sp<StateOverride>`. The caller may read through
it directly (`state->override<T>(id)`) or fork it via `copy(UNIQUE)` for
"what if" exploration.

### Block-builder loop

A single dedicated `std::thread`, started in the constructor and stopped
in the destructor:

1. Sleeps until enough time has passed since the last commit
   (`targetBlockTimeMs`).
2. Under the mempool mutex, hands a mutable reference to the mempool plus
   `targetThroughput` and a deadline to `MEVBuilder::buildBlock`. The
   builder removes selected transactions from the mempool.
3. Calls `backend.addBlock(selected)`.
4. Calls `rebuildSpeculativeState()`, which:
   - Rebuilds `state` from `backend.newStateOverride()`.
   - Re-applies each remaining mempool transaction in order to restore
     the speculative view.
5. Persists the trimmed mempool to `mempool.bin`.

The block-builder thread is **not** scheduled through `libexcessive`'s
`ThreadPool`: persistent threads use raw `std::thread`. The `ThreadPool`
is reserved for the segment merges and catalog writes that arrive in
**(Phase 2+)**.

### Mempool persistence

`mempool.bin` lives in the backend's data directory and is the only file
the frontend touches. Its format is a header plus per-transaction frames
written through each transaction's `write()` method. On startup the frame
type byte dispatches into the per-backend `TransactionTypeRegistry` to
reconstruct each transaction.

### Threading and shutdown

- `sendTransaction`, `getTransactionsByTimeWindow`, `getTransactionById`,
  `getState`, and `getMempoolSize` are safe to call from any thread.
- The destructor sets `running = false`, joins the block-builder thread,
  and saves the mempool one final time before returning.

## Mapping to the original architecture

| Old                                                 | Phase 1                                                                                      |
|-----------------------------------------------------|----------------------------------------------------------------------------------------------|
| `BlockchainStateSnapshot` (cumulative in-RAM state) | Removed. State lives in indexes; pending state lives in `StateOverride`.                     |
| `getLatestState()`                                  | `newStateOverride()` returns a fresh override for callers to fork.                           |
| Global static transaction type registry             | Per-backend `TransactionTypeRegistry`, populated by `ChainDesign::registerTransactionTypes`. |
| Reapplied transaction history on startup            | Stopgap journal replay into `committedInterim`; goes away when indexes are segment-backed.   |
| Frontend touching multiple files                    | Only `mempool.bin`.                                                                          |
