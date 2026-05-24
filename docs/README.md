# LibMagelessChain Documentation

These documents describe the **Phase 1** state of LibMagelessChain: the
index-centric architectural core has landed (typed registries, ChainDesign,
StateOverride, Transaction reshape, Backend/Frontend/MEVBuilder rewrites) but
the segment storage layer (Catalog, ContainerManager, FreeSpaceFile-backed
packing, TimeIndex) is not yet implemented. Where a topic is deferred it is
marked **(Phase 2+)** so application code can be written today against the
final shape of the API without having to be rewritten later.

For the high-level pitch and the five-minute "Try it" example, see the
top-level [`README.md`](../README.md). For the in-progress next-phase plan,
see [`concrete_architecture_proposal.md`](../concrete_architecture_proposal.md).

## Reading order

For an application author starting from scratch:

1. [Architecture overview](architecture.md): what the pieces are, how a
   block flows from `sendTransaction` to commit, and what Phase 1 actually
   includes vs. defers.
2. [ChainDesign](chain-design.md): the single virtual interface your
   application implements to describe its chain.
3. [Transactions](transactions.md): writing `Transaction` subclasses,
   serialization, validity, value scoring, apply semantics.
4. [State overrides](state-overrides.md): how transactions read and write
   pending state, and how MEV forking interacts with `sp<T>` copy-on-write.
5. [Indexes](indexes.md): the index interface, the override family interface,
   the registration model, and what storage looks like in Phase 1 vs. later.
6. [MEV builder](mev-builder.md): how candidate block orderings are
   explored and scored.
7. [Backend and frontend](backend-and-frontend.md): the two top-level
   handles, their responsibilities, and what each does and does not own.

## Phase 1 status, in one paragraph

The library is operating on the right *shape* but with a stopgap storage
layer: state is reconstructed by replaying the epoch journal into an
internal `StateOverride` (called `committedInterim` in the backend), and
`newStateOverride()` returns a deep copy of it. Once segment-backed indexes
land, that replay disappears and `newStateOverride()` returns an empty
override that reads through the committed indexes directly. The public API
of the backend, frontend, MEVBuilder, ChainDesign, BackendRegistry,
StateOverrideRegistry, StateOverride, and Transaction is the final Phase 2
shape: nothing application-facing should change when the storage layer is
swapped in.

## What is not documented here yet

The following live entirely in [`concrete_architecture_proposal.md`](../concrete_architecture_proposal.md)
and will be documented in `docs/` only once their implementations land:

- The on-disk Catalog (per-catalog-file BTree + top-level table-of-contents
  BTree + 256-bit bloom bitmask).
- The ContainerManager and the `FreeSpaceFile`-backed sub-2 GiB packing
  policy.
- `BlockchainIndex` segment lifecycle orchestration (open, write, merge,
  encodingVersion rejection).
- Compaction policy and threadpool integration.
- `TimeIndex`: the library-provided tree-segmented time index.
- Crash recovery and the "index-degraded" block state.
