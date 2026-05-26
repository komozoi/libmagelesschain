/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-15
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef LIBMAGELESSCHAIN_BLOCKCHAINBACKEND_H
#define LIBMAGELESSCHAIN_BLOCKCHAINBACKEND_H

#include <string>

#include "BlockchainConfig.h"
#include "Logger.h"
#include "Transaction.h"
#include "ChainDesign.h"
#include "BackendRegistry.h"
#include "StateOverrideRegistry.h"
#include "StateOverride.h"
#include "TransactionTypeRegistry.h"
#include "storage/Catalog.h"
#include "storage/IndexContainerManager.h"
#include "alloc/pointer.h"
#include "ds/ArrayList.h"
#include "ds/HashMap.h"
#include "fs/FdHandle.h"


struct blockchain_metadata_header_t;

struct block_header_t {
	uint64_t millis;
	uint16_t numTransactions;
	uint8_t reserved[54];
};

/*
 * Backend storage layer.  Owns the durable epoch journal, the typed index
 * registry, the override-family registry, and the per-backend transaction
 * type registry.
 */
class BlockchainBackend {
public:
	BlockchainBackend(Logger& logger, const std::string& dataDir, sp<ChainDesign> design, BlockchainConfig config = {});

	BlockchainBackend(BlockchainBackend const&) = delete;
	BlockchainBackend(BlockchainBackend&&) = delete;

	long addBlock(const ArrayList<sp<Transaction>>& transactions);
	ArrayList<sp<Transaction>> getBlock(uint64_t blockNumber);
	ArrayList<sp<Transaction>> getTransactionsByTimeWindow(uint64_t startMillis, uint64_t endMillis);

	long getBlockHeight() const;
	uint64_t getLastBlockTimestamp() const;
	uint64_t getLastBlockTime() const;

	const BlockchainConfig& getConfig() const { return config; }
	const std::string& getDataDir() const { return dataDir; }

	/*
	 * Build a fresh StateOverride representing the committed chain state
	 * with no pending modifications.  The frontend uses this as the base
	 * for its speculative mempool view, and MEVBuilder uses it as the base
	 * for forked candidates.
	 */
	sp<StateOverride> newStateOverride() const;

	/*
	 * Typed access to a registered Index instance.  Returns null sp<T> if
	 * no instance of type T with that id has been registered.
	 */
	template<typename T>
	sp<T> index(uint8_t id) const {
		return indexes.getIndex<T>(id);
	}

	const TransactionTypeRegistry& getTransactionTypeRegistry() const { return txTypes; }
	const StateOverrideRegistry& getStateOverrideRegistry() const { return overrideReg; }

	~BlockchainBackend();

private:
	MmapHandle* getEpochFile(uint64_t blockNumber);
	uint32_t getBlockOffset(uint64_t blockNumber);

	/*
	 * Wire every registered index up to its storage by calling
	 * BlockchainIndex::attach() with the catalog, container manager, and
	 * the index's (persistentTypeId, instanceId) coordinates.  After this
	 * returns, indexes can answer queries by range-scanning the catalog
	 * and mmaping their own segment payloads on demand.  No transaction
	 * replay and no in-RAM state reconstruction at startup.
	 */
	void attachIndexesToStorage();

	/*
	 * Attach every override family in `state` to its matching registered
	 * index so seal() and read-through accessors can resolve committed
	 * state on demand via the catalog.  Pairs the i-th override family
	 * positionally with the i-th registered index, matching the
	 * registration-order contract documented on ChainDesign.
	 */
	void attachOverrideFamilies(StateOverride& state) const;

	/*
	 * Seal the override into segments and persist them.  Walks override
	 * families in registration order, pairs them positionally with indexes
	 * (the i-th override family corresponds to the i-th index registered
	 * by the ChainDesign), and for each non-empty seal payload writes the
	 * payload through the container manager and inserts a catalog entry.
	 * Indexes pick up the new segment lazily on their next query by
	 * range-scanning the catalog and mmaping the payload from the
	 * container.
	 */
	void sealOverrideToSegments(StateOverride& state, uint64_t blockNumber);

	/*
	 * If the index instance has more segments than the configured
	 * threshold, merge the two smallest mergeable segments (each under
	 * maxMergeableSegmentBytes) into one.  The catalog records the merged
	 * output, then the inputs are removed from the catalog entirely
	 * (Catalog::remove) and their disk regions returned to the index's
	 * FreeSpaceFile.  The merged output authoritatively covers the
	 * combined block range of its inputs.
	 */
	void maybeCompactIndex(uint16_t persistentTypeId, uint8_t instanceId, uint64_t currentBlock);

	BlockchainConfig config;
	std::string dataDir;
	HashMap<uint32_t, sp<MmapHandle>> openEpochs;
	MmapHandle metadataFile;
	blockchain_metadata_header_t* header;

	sp<ChainDesign> design;
	BackendRegistry indexes;
	StateOverrideRegistry overrideReg;
	TransactionTypeRegistry txTypes;

	sp<Catalog> catalog;
	sp<IndexContainerManager> containerManager;

	LogEndpoint log;

	uint64_t lastBlockTime = 0;
};


#endif //LIBMAGELESSCHAIN_BLOCKCHAINBACKEND_H
