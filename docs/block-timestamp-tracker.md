# Block Timestamp Tracker

The `BlockTimestampTracker` is a persistent index that allows querying block numbers by their mining timestamp. It uses a `BTree` to store the mapping from timestamp to block number.

## Purpose

The primary purpose of this class is to support time-windowed queries on the blockchain, such as `getTransactionsByTimeWindow`. By indexing blocks by timestamp, we can efficiently find the range of blocks that were committed within a specific time period.

## On-Disk Format

The tracker maintains a file `timestamps.bin` in the blockchain's data directory. This file is a libexcessive `BTree` containing `block_timestamp_entry_t` elements.

### `block_timestamp_entry_t`

Each entry in the BTree consists of:
- `timestamp`: The uint64_t millisecond timestamp when the block was mined.
- `blockNumber`: The uint64_t block number.

Entries are ordered lexicographically by `(timestamp, blockNumber)`. This handles the case where multiple blocks might share the same timestamp (e.g., if they are committed very quickly or if the system clock doesn't advance).

## Integration

- **Writing**: Every time a new block is committed via `BlockchainBackend::addBlock`, an entry is added to the `BlockTimestampTracker`.
- **Querying**: `BlockchainBackend::getTransactionsByTimeWindow` uses the tracker to find all block numbers within the requested time range and then retrieves the transactions from those blocks.
