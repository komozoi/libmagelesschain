# Transactions

A `Transaction` is an application-defined record that, when applied, reads
and writes pending state through a `StateOverride`. The library never
inspects a transaction's contents directly: it only ever asks the
transaction to verify itself, score itself, apply itself, serialize itself,
or report its size, type id, or timestamp.

## Interface

```cpp
class Transaction {
public:
    virtual bool   verify(const StateOverride& state) const = 0;
    virtual bool   apply(StateOverride& state) const        = 0;
    virtual float  computeValue(const StateOverride& state) const = 0;

    virtual void     write(MmapHandle* dst) const = 0;
    virtual size_t   size() const                 = 0;
    virtual uint8_t  getTypeId() const            = 0;
    virtual uint64_t getTimestamp() const         = 0;
};
```

### `apply`

Performs the transaction's pending state changes through the supplied
override. Return `true` on success, `false` on failure. Failed
transactions are still committed to the journal (the block is queryable,
the transaction is byte-identical to a successful one on disk) but they
contribute *no* state delta. They can still be looked up by id or returned
from time-window queries.

Within `apply` you access the relevant override family with
`s.override<MyOverrideFamily>(id)`. See [state overrides](state-overrides.md)
for the full pattern.

The override exposed to `apply` reflects the chain *as if* all overrides
in this block-in-progress had committed. That means later transactions
within the same block see the effects of earlier transactions in the same
block. Execution order matters.

### `verify`

Should not mutate the override. The commit path calls `verify` during the
transient state verification phase in `addBlock`. Transactions that fail
`verify` will cause `addBlock` to reject the entire block. Applications
should still use `verify` for mempool admission filtering before calling
`sendTransaction`.

This method checks that the transaction is valid against the current state,
but not necessarily that the transaction will succeed when one attempts to
commit it (which is determined by `apply`).

### `computeValue`

Scores this transaction for MEV ordering against the supplied override.
Returns a `float`; higher is more valuable. MEVBuilder calls this
*repeatedly*, re-evaluating after each pick so a transaction whose value
depends on whether some other transaction has executed first is scored
correctly each time. See [mev-builder.md](mev-builder.md) for the precise
algorithm.

### `write`

Serialize the transaction into the destination `MmapHandle`. The first
byte written **must** be the result of `getTypeId()` so that the
`TransactionTypeRegistry::read` path can dispatch back to the correct
subclass on read. `size()` must return the same number of bytes that
`write` actually produces.

### `getTimestamp`

Returns the millis-since-epoch timestamp the transaction was created at.
Used by `getTransactionsByTimeWindow` for mempool filtering. Committed
transactions are also queryable by the block's commit timestamp in
addition to the per-transaction stamp; see
[backend-and-frontend.md](backend-and-frontend.md).

## Type registration

Transactions are deserialized through the per-backend
`TransactionTypeRegistry`. Inside `ChainDesign::registerTransactionTypes`:

```cpp
registry.registerType(/*typeId=*/ 1, &TransferTransaction::read);
```

`TransferTransaction::read` (or any free function with signature
`sp<Transaction>(MmapHandle*)`) is expected to consume the same bytes
`write` produced, including the 1-byte typeId prefix. A common pattern is
to peek the first byte to confirm the id matches and then read the rest:

```cpp
sp<Transaction> TransferTransaction::read(MmapHandle* src) {
    uint8_t typeId = readU8(src);   // consume the type prefix
    sp<TransferTransaction> tx(sp<TransferTransaction>::create());
    // ... read fields into tx.mut()
    return sp<Transaction>(std::move(tx));
}
```

The registry is **per-backend**: each `BlockchainBackend` has its own
`TransactionTypeRegistry`. Two different backends in the same process can
have completely different type id maps. (In earlier revisions the registry
was a global static; that caused cross-test contamination and made
multi-design hosts impossible.)

## Sending a transaction

The frontend is the entry point:

```cpp
sp<Transaction> tx(sp<TransferTransaction>::create(/* ... */));
frontend.sendTransaction(tx);
```

`sendTransaction` does three things atomically (under the mempool mutex):

1. Adds the transaction to the in-memory mempool.
2. Applies it to the frontend's speculative `StateOverride` so subsequent
   reads through `frontend.getState()` see the pending changes.
3. Persists the mempool to `mempool.bin` so a restart can resume from the
   same speculative state.

If `apply` returns `false` on insertion the transaction is still added (it
will commit but produce no state delta). Applications that want to reject
invalid transactions before they touch the mempool should call `verify`
themselves prior to `sendTransaction`.

## Querying transactions

- `frontend.getTransactionById(uint64_t id)`: id is `(blockNumber << 20) |
  txIndexInBlock`. Returns the transaction from the committed block, or
  `nullptr` for ids that have not been committed.
- `frontend.getTransactionsByTimeWindow(start, end)`: returns all
  committed transactions whose block timestamp falls in `[start, end]`,
  *plus* mempool transactions whose own `getTimestamp()` falls in
  `[start, end]`.

The backend offers `getBlock(blockNumber)` and
`getTransactionsByTimeWindow(start, end)` without the mempool tail.

## What changed from pre-Phase-1

- `apply` now takes a `StateOverride&` instead of a `BlockchainStateSnapshot&`.
- `verify` likewise takes a `const StateOverride&`.
- `computeValue` takes a `const StateOverride&` and is called repeatedly
  by MEVBuilder; subclasses should not assume it is only called once.
- The transaction type registry is per-backend, populated by ChainDesign,
  and no longer a global static.
