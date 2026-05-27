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

#include "BlockchainBackend.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <stdexcept>

#include "universaltime.h"

#include "storage/EpochFile.h"


#define LONG_TIME_MILLIS (1000 * 3600)
#define WANTED_TX_PER_BLOCK 180
#define MAX_TX_PER_BLOCK 1024


struct blockchain_metadata_header_t {
	uint32_t version;
	uint64_t lastBlockTimestamp;
	long currentBlockHeight;
};


sp<EpochFile> epochBuilder(const FdHandle& handle) {
	return sp<EpochFile>::create(handle);
}


BlockchainBackend::BlockchainBackend(Logger& logger, const std::string& dataDir, sp<ChainDesign> design, BlockchainConfig config)
	: config(config), dataDir(dataDir), epochCache(dataDir + "/epochs/", epochBuilder), design(std::move(design)), executor(2), log(logger, "BlockchainBackend") {
	mkdir(dataDir.c_str(), 0770);
	timestampTracker = sp<BlockTimestampTracker>::create(dataDir);
	mkdir((dataDir + "/epochs").c_str(), 0770);
	FdHandle metadataHandle = FdHandle::open((dataDir + "/metadata.bin").c_str(), O_RDWR | O_CREAT, 0660);
	metadataFile = metadataHandle.getMmapHandle(0, sizeof(blockchain_metadata_header_t));
	header = metadataFile.directPointer<blockchain_metadata_header_t>();
	if (metadataHandle.isNew()) {
		*header = {CURRENT_FILE_VERSION, 0, 0};
	}

	// Populate registries from the application's chain design.  Order
	// matters: indexes, then overrides, then transaction types.
	this->design.mut().registerIndexes(indexes);
	this->design.mut().registerOverrides(overrideReg);
	this->design.mut().registerTransactionTypes(txTypes);

	// Storage layer: the catalog owns its directory tree and all on-disk
	// segment metadata (TOC BTree + per-file BTrees).  Segment payload
	// bytes live inside the catalog files themselves, packed via the
	// catalog file's FreeSpaceFile.
	catalog = sp<Catalog>::create(dataDir + "/catalog", executor);

	// Wire each registered index to the catalog so it can answer queries
	// by mmap'ing segment payloads on demand.  No transaction replay and
	// no in-RAM state reconstruction at startup; opening the chain costs
	// O(catalog metadata) regardless of chain size.
	attachIndexesToStorage();
}

void BlockchainBackend::attachIndexesToStorage() {
	const ArrayList<BackendRegistry::Entry>& entries = indexes.getEntries();
	for (int i = 0; i < entries.size(); ++i) {
		uint16_t indexId = (uint16_t)i;
		sp<BlockchainIndex> idx = indexes.getIndexAt(indexId);
		if (!idx) continue;
		idx.mut().attach(catalog.get(), indexId);
	}
}

void BlockchainBackend::attachOverrideFamilies(StateOverride& state) const {
	int n = state.familySize();
	const ArrayList<BackendRegistry::Entry>& indexEntries = indexes.getEntries();
	int pairCount = (n < indexEntries.size()) ? n : indexEntries.size();
	for (int i = 0; i < pairCount; ++i) {
		sp<BlockchainIndex> idx = indexes.getIndexAt((uint16_t)i);
		if (!idx) continue;
		state.familyAt(i).attach(idx.mut());
	}
}

sp<StateOverride> BlockchainBackend::newStateOverride() const {
	// Build a fresh, empty override and wire each family up to its
	// matching index so read-through accessors and seal() can resolve
	// committed state on demand via the catalog.  The committed state
	// itself lives on disk in segments and is read lazily by the index.
	sp<StateOverride> s = sp<StateOverride>(UNIQUE, overrideReg);
	attachOverrideFamilies(s.mut());
	return s;
}

long BlockchainBackend::addBlock(const ArrayList<sp<Transaction>>& transactions) {
	uint64_t millis = millis_since_epoch();
	uint64_t duration = millis - header->lastBlockTimestamp;

	// Skip timing checks if we already have the maximum per block - maximize throughput in this case
	if (transactions.size() != MAX_TX_PER_BLOCK) {
		// Basic checks: Has enough time elapsed since the last block?
		// At defaults: Average block time should be 60 seconds.  Most systems will have ~180-240 transactions per minute.
		// Transaction rate varies, so block time should go down if there are too many transactions.
		// Thus, if the transaction queue has config.targetThroughput transactions, the throughput will be that many transactions per minute.
		//  Throughput = numTransactions / blockTime
		//  ThroughputWanted = numTransactions^2 / config.targetThroughput
		// Thus:
		//  blockTime = targetBlockTime * targetThroughput / numTransactions
		// Block time should stay in the range of 100ms to 600s.
		uint64_t blockTime = std::max(std::min((uint64_t)config.targetBlockTimeMs * config.targetThroughput / (uint32_t)transactions.size(), (uint64_t)600 * 1000), (uint64_t)100);
		if (duration < blockTime || (duration > LONG_TIME_MILLIS && transactions.size() > WANTED_TX_PER_BLOCK/2) || transactions.size() > MAX_TX_PER_BLOCK)
			// Wait for the configured cadence before sealing this block.
			return -1;
	}

	// First, verify the transactions with a transient state
	sp<StateOverride> verificationOverride = newStateOverride();
	for (const sp<Transaction>& tx : transactions) {
		if (!tx->verify(*verificationOverride)) {
			throw std::runtime_error("Transaction verification failed");
		}
		tx->apply(verificationOverride.mut());
	}

	// Get general epoch file setup/info
	uint64_t blockNumber = header->currentBlockHeight++;
	sp<EpochFile> epochFile = getEpochFile(blockNumber);

	// Actually write the block to disk
	epochFile.mut().appendBlock(blockNumber, millis, transactions);
	header->lastBlockTimestamp = millis;
	timestampTracker.mut().addBlock(millis, blockNumber);

	// Now seal the speculative state into per-index segments.  This
	// applies the transactions' effects to the in-RAM index state, writes
	// the resulting payload through the container manager, and inserts a
	// catalog entry for each touched index.  We use a fresh override that
	// starts from the just-loaded committed state, then applies the
	// block's transactions exactly once.  This is the canonical
	// "simulation becomes commit" flow.
	sp<StateOverride> blockOverride = newStateOverride();
	for (const sp<Transaction>& tx : transactions) {
		tx->apply(blockOverride.mut());
	}
	sealOverrideToSegments(blockOverride.mut(), blockNumber);

	log.debug("New block mined with %u transactions at %u transactions per minute.", transactions.size(), (uint32_t)(transactions.size() * 60000 / duration));

	msync(header, sizeof(blockchain_metadata_header_t), MS_SYNC);

	lastBlockTime = duration;

	return (long)blockNumber;
}

void BlockchainBackend::sealOverrideToSegments(StateOverride& state, uint64_t blockNumber) {
	const ArrayList<BackendRegistry::Entry>& indexEntries = indexes.getEntries();
	int n = state.familySize();
	int pairCount = (n < indexEntries.size()) ? n : indexEntries.size();
	for (int i = 0; i < pairCount; ++i) {
		IndexOverrideFamilyBase& fam = state.familyAt(i);
		Bytestring payload = fam.seal();
		if (payload.size() == 0) continue;

		sp<BlockchainIndex> idx = indexes.getIndexAt((uint16_t)i);
		if (!idx) continue;

		// Route the sealed payload to the catalog.  It picks a catalog
		// file by locality + capacity and writes the segment in the
		// background via the executor.  Indexes pick up the new segment
		// lazily on their next query through Catalog::rangeScan +
		// CatalogFile::openForReading.
		catalog.mut().writeSegment((uint16_t)i, idx->encodingVersion(), 0, blockNumber, blockNumber + 1, std::move(payload));
	}
}


ArrayList<sp<Transaction>> BlockchainBackend::getBlock(uint64_t blockNumber) {
	ArrayList<sp<Transaction>> results;
	if (blockNumber >= (uint64_t)header->currentBlockHeight)
		throw std::range_error("Block number out of range");

	sp<EpochFile> epochFile = getEpochFile(blockNumber);
	if (!epochFile)
		throw std::runtime_error("Failed to open epoch file");

	return epochFile.mut().readBlock(blockNumber, txTypes);
}

ArrayList<sp<Transaction>> BlockchainBackend::getTransactionsByTimeWindow(uint64_t startMillis, uint64_t endMillis) {
	if (startMillis > endMillis)
		throw std::invalid_argument("Start time must be before end time");

	ArrayList<uint64_t> blockNumbers = timestampTracker.mut().getBlocksInWindow(startMillis, endMillis);
	ArrayList<sp<Transaction>> allTransactions;
	for (int i = 0; i < blockNumbers.size(); ++i) {
		ArrayList<sp<Transaction>> blockTxs = getBlock(blockNumbers.get(i));
		for (int j = 0; j < blockTxs.size(); ++j) {
			allTransactions.add(blockTxs.get(j));
		}
	}
	return allTransactions;
}


long BlockchainBackend::getBlockHeight() const {
	return header->currentBlockHeight;
}

uint64_t BlockchainBackend::getLastBlockTimestamp() const {
	return header->lastBlockTimestamp;
}

uint64_t BlockchainBackend::getLastBlockTime() const {
	return lastBlockTime;
}

sp<EpochFile> BlockchainBackend::getEpochFile(uint64_t blockNumber) {
	uint32_t epochNumber = (uint32_t)(blockNumber / BLOCKS_PER_EPOCH);
	return epochCache.open(std::to_string(epochNumber) + ".bin", O_RDWR | O_CREAT);
}

BlockchainBackend::~BlockchainBackend() {
	// Drain any background segment writes before tearing the catalog
	// down, since catalog tasks reference Catalog/CatalogFile members.
	executor.shutdown();
}
