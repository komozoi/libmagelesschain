# ChainDesign

`ChainDesign` is the single virtual interface your application implements
to describe the shape of its chain. The library is otherwise oblivious to
your data model; everything else (backend, frontend, MEV builder, state
overrides, registries) is parameterized by the design.

## Interface

```cpp
class ChainDesign {
public:
    virtual ~ChainDesign() = default;

    virtual void registerIndexes(BackendRegistry& registry) = 0;
    virtual void registerOverrides(StateOverrideRegistry& registry) = 0;
    virtual void registerTransactionTypes(TransactionTypeRegistry& registry) = 0;
};
```

The design is passed to `BlockchainBackend` as a `sp<ChainDesign>`. The
backend calls the three hooks once, in order, during construction.
Registration order is meaningful:

- Within `registerIndexes`, successive `registerIndex<T>(instance)` calls
  allocate the next `uint8_t` id under the family for `T` (`0`, `1`, ...).
- Within `registerOverrides`, successive `registerOverride<T>()` calls
  allocate the next id under the family for `T`.
- Within `registerTransactionTypes`, every transaction subclass associates
  itself with a 1-byte `typeId`.
- At commit time, override families are sealed in the same order they were
  registered.

The design instance lives alongside the backend for the chain's lifetime.
It is never owned by the library; the application typically holds an
`sp<ChainDesign>` together with its `BlockchainBackend`.

## Minimum example

```cpp
class MyChainDesign : public ChainDesign {
public:
    void registerIndexes(BackendRegistry& registry) override {
        registry.registerIndex<BalanceIndex>(sp<BalanceIndex>::create());
    }
    void registerOverrides(StateOverrideRegistry& registry) override {
        registry.registerOverride<BalanceOverride>();
    }
    void registerTransactionTypes(TransactionTypeRegistry& registry) override {
        registry.registerType(/*typeId=*/ 1, &TransferTransaction::read);
    }
};
```

Each `BalanceIndex` registered under id `0` pairs with the `BalanceOverride`
registered under id `0`. They share their `(TypeKey, uint8_t)` slot and the
backend will route transactions through it.

## Rules and constraints

- **One override family per index instance.** Each `registerIndex<T>(...)`
  call should be matched by a `registerOverride<U>()` call (typically a
  matching pair like `BalanceIndex` / `BalanceOverride`). The library does
  not enforce a particular pairing, but transactions reach an override
  through `s.override<U>(id)` and reach the committed-side equivalent
  through `backend.index<T>(id)`, so the application is responsible for
  keeping the two registrations consistent.
- **Ids are dense per type.** Multiple registrations of the same `T` get
  `0, 1, 2, ...`. Do not assume a specific instance can be addressed by
  any other key.
- **No defaults.** `backend.index<T>(id)` and `state.override<T>(id)` do
  not assume `id = 0`. Always pass it explicitly.
- **No commit-order hook.** Sealing order is registration order; there is
  no separate `commitOrder` API.

## Lifetime and threading

- `ChainDesign` callbacks run only during `BlockchainBackend` construction,
  on the thread that built the backend.
- After construction the design pointer is held but no further methods are
  called by the library. It is safe for the application to keep
  using the same `sp<ChainDesign>` for inspection or as a factory for the
  application's own purposes.

## See also

- [Indexes](indexes.md): writing the `BlockchainIndex` subclasses
  you pass to `registerIndex<T>`.
- [State overrides](state-overrides.md): writing the
  `IndexOverrideFamilyBase` subclasses you pass to `registerOverride<T>`.
- [Transactions](transactions.md): writing the deserialization factory you
  pass to `registerType`.
