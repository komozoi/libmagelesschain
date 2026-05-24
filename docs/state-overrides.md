# State overrides

A `StateOverride` is the typed bundle of pending state modifications for
one block-in-progress. It replaces the legacy `BlockchainStateSnapshot`
type entirely: there is no longer any cumulative in-RAM chain state in the
library.

A state override is *not* a snapshot of the whole chain. It carries only
the deltas that pending transactions have produced on top of the committed
chain state. Reads for untouched data fall through to the committed
indexes.

## What's in a StateOverride

The library type `StateOverride` is a concrete container that holds one
`sp<IndexOverrideFamilyBase>` per registered `(TypeKey, uint8_t id)` slot.
Each slot's concrete type is whatever the application registered via
`ChainDesign::registerOverrides`. Internally:

```cpp
HashMap<TypeKey, ArrayList<sp<IndexOverrideFamilyBase>>> families;
ArrayList<Slot> order;   // preserves registration order for sealing
```

Two operations are exposed to application code:

```cpp
template<typename T> T&       override(uint8_t id);
template<typename T> const T& override(uint8_t id) const;
```

Inside a transaction:

```cpp
bool TransferTransaction::apply(StateOverride& s) const override {
    BalanceOverride& bal = s.override<BalanceOverride>(0);
    if (!bal.canDebit(from, amount)) return false;
    bal.debit(from, amount);
    bal.credit(to, amount);
    return true;
}
```

No casts. The library performs the one cast from `IndexOverrideFamilyBase`
to `T` inside `override<T>()`; application code receives a strongly typed
reference.

## Writing an override family

An `IndexOverrideFamilyBase` subclass is the part of your chain design
that *defines* the API a transaction uses to read and write pending state
for one kind of index. The base interface is intentionally tiny:

```cpp
class IndexOverrideFamilyBase {
public:
    virtual ~IndexOverrideFamilyBase() = default;
    virtual Bytestring seal() const { return Bytestring(); }
};
```

Everything else, including all read-through to committed state, all
pending-change bookkeeping, and all per-block deltas, is application
defined. A typical subclass:

```cpp
class BalanceOverride : public IndexOverrideFamilyBase {
public:
    BalanceOverride() = default;
    BalanceOverride(const BalanceOverride& other) = default;

    int64_t balanceOf(uint64_t account) const {
        int64_t* pending = changes.getPtr(account);
        if (pending) return *pending;
        return /* read through to the committed BalanceIndex */;
    }
    bool canDebit(uint64_t account, int64_t amount) const { /* ... */ }
    void debit(uint64_t account, int64_t amount) { /* ... */ }
    void credit(uint64_t account, int64_t amount) { /* ... */ }

    Bytestring seal() const override { /* (Phase 2+) serialize 'changes' */ }

private:
    HashMap<uint64_t, int64_t> changes;
};
```

### Copy semantics

`StateOverride` is wrapped in an `sp<StateOverride>` and forked freely via
libexcessive's copy-on-write. **Every override family subclass must be
copy-constructible**, ideally cheaply. When the frontend or MEVBuilder
mutates one override family in a forked override, only that family is
deep-copied. All other families remain shared with the original.

The library does not need a `clone()` method. `sp<T>` handles cloning via
the copy constructor when `mut()` is called and the ref count is greater
than one.

### `seal()`

In Phase 1 `seal()` is allowed to return an empty `Bytestring` and the
library does not yet write its output to disk. When the segment-storage
layer lands **(Phase 2+)**, `seal()` will be called once per
`(type, id)` slot at block commit time, and the returned bytes will become
the segment payload for that index instance. Returning an empty
`Bytestring` will signal "no changes for this index in this block" and
will skip the catalog entry.

## How the frontend uses StateOverride

The frontend owns one `sp<StateOverride>` reflecting the speculative chain
state:

```
speculative = backend.newStateOverride() then apply every mempool tx in order
```

This object is rebuilt after every commit. Until then, `sendTransaction`
calls `apply` against it directly so reads through `frontend.getState()`
always reflect every pending mempool transaction.

## How MEVBuilder uses StateOverride

When the block builder thread asks `MEVBuilder` for a block, the builder
gets a fresh `sp<StateOverride>` from `backend.newStateOverride()` (i.e.
no mempool applied) and iteratively:

1. Calls `computeValue(*candidate)` on every remaining mempool transaction.
2. Picks the highest scorer and `apply()`s it to `candidate`.
3. Repeats until the block size target is reached or the mempool is empty.

Because the override is wrapped in `sp<StateOverride>`, the builder can
fork cheaply via `copy(UNIQUE)` for what-if exploration. Only the families
a candidate mutates get deep-copied; the rest are aliased through `sp<T>`
CoW.

For this to work correctly the override family must be deterministic about
when it allocates. If `BalanceOverride::balanceOf` lazily caches the
committed read into the override on first call, that cache is a write
the CoW will detach. That is usually fine, but if your family is huge and
the cache is large, prefer not to materialize the entire committed state
on read.

## What changed from pre-Phase-1

- `BlockchainStateSnapshot` is gone. Application code that referenced it
  must move to a `StateOverride` plus one or more `IndexOverrideFamilyBase`
  subclasses.
- The override is the *only* state visible to a `Transaction`. There is no
  in-RAM "current state" for transactions to read; if you want committed
  data, the override family reads through to the relevant `Index` on
  demand.
- `sp<T>` CoW (via `sp<T>(UNIQUE)` and `.mut()`) replaces all explicit
  `clone()` methods.
