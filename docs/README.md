# LibMagelessChain Documentation

These documents describe the current state of LibMagelessChain. Both the
index-centric architectural core (typed registries, `ChainDesign`,
`StateOverride`, the reshaped `Transaction`, Backend/Frontend/MEVBuilder)
and the segment storage layer (`Catalog`, `IndexContainerManager`, the
`BlockchainIndex` segment lifecycle, threshold-driven compaction) are
implemented, including the multi-file catalog with a table-of-contents
BTree and a 256-bit bloom bitmask per catalog file, and packed
`FreeSpaceFile`-backed containers shared across indexes. A handful of
refinements remain (`TimeIndex`, parallel compaction via `ThreadPool`,
crash-time index-degraded recovery); each is called out where relevant.

For the high-level pitch and the five-minute "Try it" example, see the
top-level [`README.md`](../README.md). For the in-progress next-phase plan,
see [`concrete_architecture_proposal.md`](../concrete_architecture_proposal.md).

## Reading order

For an application author starting from scratch:

1. [Architecture overview](architecture.md): what the pieces are, how a
   block flows from `sendTransaction` to commit, and a high-level view.
2. [ChainDesign](chain-design.md): the single virtual interface your
   application implements to describe its chain.
3. [Transactions](transactions.md): writing `Transaction` subclasses,
   serialization, validity, value scoring, apply semantics.
4. [State overrides](state-overrides.md): how transactions read and write
   pending state, and how MEV forking interacts with `sp<T>` copy-on-write.
5. [Indexes](indexes.md): the index interface, the override family
   interface, the registration model, the segment lifecycle, and the
   on-disk storage layout.
6. [MEV builder](mev-builder.md): how candidate block orderings are
   explored and scored.
7. [Backend and frontend](backend-and-frontend.md): the two top-level
   handles, their responsibilities, and what each does and does not own.

## Status, in one paragraph

The chain now writes through real segment-backed indexes: each block's
sealed override-family payloads are stored via `IndexContainerManager`
and catalogued in `Catalog`. Indexes hold no committed state in RAM;
they attach to the catalog and container manager at registration time
and mmap segments lazily at query time. Neither startup nor reads
replay any transactions. The public API of the backend, frontend,
MEVBuilder, ChainDesign, BackendRegistry, StateOverrideRegistry,
StateOverride, and Transaction is the final intended shape; nothing
application-facing should change when the remaining refinements land
