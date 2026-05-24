# MEVBuilder

`MEVBuilder` is the library's block construction component. It explores
candidate orderings of mempool transactions to maximize their total
computed value against a forked `StateOverride`. "Value" is whatever
`Transaction::computeValue` returns; the library treats it as an opaque
float to maximize and does not impose a particular economic model.

## Interface

```cpp
class MEVBuilder {
public:
    MEVBuilder(BlockchainBackend& backend);

    ArrayList<sp<Transaction>> buildBlock(
        ArrayList<sp<Transaction>>& mempool,
        uint16_t maxTransactions,
        uint64_t deadline) const;
};
```

- `mempool`: candidate transactions. Selected transactions are *removed*.
- `maxTransactions`: upper bound on transactions per block.
- `deadline`: wall-clock millis at which to stop the optimization loop.
  Pass `0` to skip the optimization loop entirely.

Returned transactions are in execution order: the order they must be
applied to recreate the value the builder scored.

## Algorithm

The builder is owned by `BlockchainFrontend` and invoked from the
block-builder thread. Each call:

1. Asks the backend for a fresh `sp<StateOverride>` via
   `backend.newStateOverride()`. This is the committed-state baseline; no
   mempool transactions have been applied to it yet.
2. Iteratively, until either `maxTransactions` is reached or the mempool
   is exhausted:
   1. Calls `computeValue(*candidate)` on every remaining mempool
      transaction.
   2. Picks the highest scorer.
   3. Calls `apply()` on it against `*candidate`, advancing the simulated
      state for the next iteration's `computeValue` calls.
   4. Removes the picked transaction from the mempool.
3. Returns the picked transactions in the order they were applied.

Steps 2.1 and 2.3 are where state-dependent value is correctly resolved:
a transaction whose value depends on whether another transaction has
already executed will be re-scored after each pick.

## Forking and copy-on-write

The override the builder operates on is the application's `StateOverride`
held inside an `sp<StateOverride>`. The builder may fork the override for
"what if" exploration by simply copying the `sp<StateOverride>`; mutating
one fork via `mut()` deep-copies only the override families that were
actually touched (see [state overrides](state-overrides.md) for the full
explanation). Untouched families remain aliased across forks.

For this to work, override family subclasses must be copy-constructible
and should not hold raw `T*` aliases into other families.

## The optimization loop

The current implementation runs the greedy pick described above. The
header documents a placeholder optimization loop that, in future work,
will attempt swap or 2-opt style improvement passes between picks while
the `deadline` budget allows. The deadline is wall-clock millis. Pass `0`
to skip the optimization loop entirely; the greedy pass always runs.

## Where MEVBuilder is used

`BlockchainFrontend` constructs a `MEVBuilder` in its constructor and
calls `buildBlock` from `blockBuilderLoop` on the background block-builder
thread. The frontend's `BlockchainConfig` provides `maxTransactions` (via
`targetThroughput`) and the per-block deadline is computed from
`targetBlockTimeMs`. Applications do not normally construct `MEVBuilder`
themselves.

## Testing

`src/blockchain/tests/test_MEVBuilder.cpp` covers:

- Capacity bounds (returns at most `maxTransactions`).
- Selection by descending value.
- State-dependent value (transactions whose value changes after a previous
  transaction has been applied).
- Execution-order correctness (the returned list, when applied in order,
  reproduces the simulated state).

Test cases use `TestChainDesign` (`TestSumIndex` and `TestSumOverrideFamily`)
to build per-test override families with non-trivial value functions.

The pending test surface for full MEV forking correctness (the case where
the builder forks for what-if exploration with `sp<T>` CoW and must not
leak mutations across siblings) is queued for the `test_MEVForking`
file in **(Phase 2+)**.
