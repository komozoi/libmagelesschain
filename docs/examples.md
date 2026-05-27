# Examples

This document provides extensive examples of how to build and interact with a blockchain using LibMagelessChain.

## Table of Contents

1. [Minimal Working Example](#minimal-working-example)
2. [State Persistence with Custom Index](#state-persistence-with-custom-index)
3. [MEV Ordering and Value Scoring](#mev-ordering-and-value-scoring)

---

## Minimal Working Example

This example demonstrates the absolute minimum required to get a blockchain up and running. Even without defining any
persistent state indexes, every transaction is automatically recorded in the durable **epoch journal** on disk. This
journal is the source of truth for the chain.

### 1. Define a Transaction

Transactions encapsulate application logic and data. In this bare-bones example, we define a transaction that stores a
simple string.

```cpp
#include <blockchain/Transaction.h>
#include <blockchain/StateOverride.h>
#include <ds/Bytestring.h>

class StringTx : public Transaction {
public:
	Bytestring data;

	StringTx(const Bytestring& d) : data(d) {}

	bool verify(const StateOverride& state) const override { return true; }
	bool apply(StateOverride& state) const override { return true; }
	float computeValue(const StateOverride& state) const override { return 1.0f; }

	void write(MmapHandle* dst) const override {
		dst->write(getTypeId());
		uint32_t len = (uint32_t)data.size();
		dst->write(len);
		if (len > 0) {
			dst->write(&data[0], len);
		}
	}

	size_t size() const override { return 1 + 4 + data.size(); }
	uint8_t getTypeId() const override { return 1; }
	uint64_t getTimestamp() const override { return 0; }

	static sp<Transaction> createFromMmap(MmapHandle* src) {
		uint32_t len = 0;
		src->read(len);
		
		uint8_t* buffer = (uint8_t*)malloc(len);
		src->read(buffer, len);
		Bytestring d(buffer, len);
		free(buffer);
		
		return sp<StringTx>::create(d);
	}
};
```

### 2. Define the Chain Design

The `ChainDesign` acts as a factory and registry for all application-specific types.

```cpp
#include <blockchain/ChainDesign.h>

class MyDesign : public ChainDesign {
public:
	void registerIndexes(BackendRegistry& registry) override {
		// No persistent indexes in this minimal example.
	}

	void registerOverrides(StateOverrideRegistry& registry) override {
		// No overrides in this minimal example.
	}

	void registerTransactionTypes(TransactionTypeRegistry& registry) override {
		registry.registerType(1, &StringTx::createFromMmap);
	}
};
```

### 3. Initialize and Run

This example sends 60 transactions, waits for the background thread to mine a block, and then verifies the transactions
by reading them back from disk.

```cpp
#include <blockchain/BlockchainFrontend.h>
#include <blockchain/BlockchainBackend.h>
#include <Logger.h>
#include <thread>

int main() {
	Logger logger("logs", 0, 0);
	
	sp<MyDesign> design = sp<MyDesign>::create();
	
	// Lower the block time to 1 second for this example.
	BlockchainConfig config;
	config.targetBlockTimeMs = 1000;
	
	BlockchainBackend backend(logger, "data", design, config);
	BlockchainFrontend frontend(backend, config);

	// 1. Send 60 random transactions.
	for (int i = 0; i < 60; ++i) {
		char buf[32];
		snprintf(buf, sizeof(buf), "Transaction #%d", i);
		Bytestring data(buf);
		frontend.sendTransaction(sp<StringTx>::create(data));
	}
	
	// 2. Wait for the block to be mined.
	LogEndpoint log(logger, "Main");
	log.info("Waiting for block to be mined...");
	while (frontend.getBlockHeight() == 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	
	// 3. Verify that the transactions can be read back.
	ArrayList<sp<Transaction>> block = backend.getBlock(0);
	log.info("Successfully read %d transactions from block 0.", block.size());
	
	return 0;
}
```

---

## State Persistence with Custom Index

While transactions are the source of truth, replaying history to find the current state is slow. A `BlockchainIndex`
allows you to persist state "segments" to the `Catalog`. This provides efficient queries and instant state recovery on
startup.

### 1. The Index Class (The Query Handle)

The index handles reading committed state segments from disk.

```cpp
#include <blockchain/BlockchainIndex.h>
#include <storage/Catalog.h>
#include <ds/Bytestring.h>

class CounterIndex : public BlockchainIndex {
public:
	uint16_t encodingVersion() const override { return 1; }

	// Custom query method to find the latest value on disk.
	int getLatestValue() const {
		if (!attachedCatalog) return 0;

		// The Catalog tracks which segments belong to this index.
		// A real implementation would scan for the most recent segment.
		return 0; 
	}

	Bytestring mergeSegments(const ArrayList<segment_coordinate_t>& inputs) const override {
		// Compaction: combine multiple segments into one.
		return Bytestring();
	}
};
```

### 2. The Persistent Override Family

The override family layers pending changes over the index and "seals" them into segments at block commit.

```cpp
class PersistentCounterOverride : public IndexOverrideFamilyBase {
public:
	int delta = 0;
	CounterIndex* index = nullptr;

	// The library wires the override to its index automatically.
	void attach(BlockchainIndex& idx) override {
		index = static_cast<CounterIndex*>(&idx);
	}

	// Returns the index value plus the pending delta.
	int value() const {
		return (index ? index->getLatestValue() : 0) + delta;
	}

	// Called by the backend to produce the persistent segment payload.
	Bytestring seal() const override {
		if (delta == 0) return Bytestring(); 

		int newValue = value();
		return Bytestring((uint8_t*)&newValue, 4);
	}
};
```

### 3. The State Perspective (StateOverride)

A `StateOverride` provides a **perspective** of the chain's state. It is a container that holds one override family
instance for every registered index.

* **Committed State**: The `CounterIndex` reads directly from segments on disk.
* **Speculative State**: The `PersistentCounterOverride` tracks pending changes. When you query `value()`, it
  read-throughs to the index and adds the local delta.

```cpp
void checkState(BlockchainFrontend& frontend) {
	// 1. Get the speculative state (includes mempool).
	sp<StateOverride> state = frontend.getState();
	
	// 2. Access the specific family for our counter.
	// Slot 0 corresponds to the first index/override registered in MyDesign.
	PersistentCounterOverride& fam = state->override<PersistentCounterOverride>(0);
	
	// 3. This value includes both committed segments and pending transactions.
	int currentVal = fam.value();
}
```

---

## MEV Ordering and Value Scoring

The `MEVBuilder` uses the `computeValue` method of transactions to decide the optimal ordering within a block.

```cpp
class DynamicValueTx : public Transaction {
public:
	int bribeAmount = 10;

	bool apply(StateOverride& state) const override {
		// Transaction logic...
		return true;
	}

	float computeValue(const StateOverride& state) const override {
		// The builder re-evaluates this after each tentative placement.
		// We can return a value that depends on the current speculative state.
		int currentVal = state.override<PersistentCounterOverride>(0).value();
		
		if (currentVal > 100) {
			// This transaction is worth more if the counter has already crossed 100.
			return (float)bribeAmount * 2.0f;
		}
		return (float)bribeAmount;
	}
	
	// ... write(), size(), getTypeId() ...
};
```

The `MEVBuilder` iteratively explores orderings, scoring each candidate block by the sum of `computeValue` results for
all transactions in that ordering.

---

## Querying the Chain

The library provides several ways to query committed and speculative state.

### 1. Querying Speculative State (Mempool + Committed)

The `BlockchainFrontend` provides the `getState()` method, which returns a `StateOverride` reflecting the mempool on top
of the committed state.

```cpp
sp<StateOverride> specState = frontend.getState();
int currentVal = specState->override<PersistentCounterOverride>(0).value();
```

### 2. Querying Committed Transactions

You can query committed transactions via the `BlockchainBackend` or the `BlockchainFrontend` (which includes the
mempool).

```cpp
// Get a block by height
ArrayList<sp<Transaction>> block = backend.getBlock(height);

// Get transactions by time window (Frontend includes mempool, Backend does not)
ArrayList<sp<Transaction>> txs = frontend.getTransactionsByTimeWindow(startTime, endTime);
```

### 3. Querying Committed State via Indexes

Indexes are accessed via the `BlockchainBackend`.

```cpp
sp<CounterIndex> index = backend.index<CounterIndex>(0);
if (index) {
	int committedVal = index->getLatestValue();
}
```
