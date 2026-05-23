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

#include <fcntl.h>

#include "universaltime.h"
#include "alloc/pointer.h"

#define FILE_VERSION_00_00_00 0xC7000000
#define CURRENT_FILE_VERSION FILE_VERSION_00_00_00

// At 1 block per minute, this is ~18.2h of runtime.
// Depending on circumstance, these files may grow rather large
// Expect file size to be 2-8GB for each epoch under advanced use cases.
// This then requires about 2TB of storage for a year of runtime.
// By then compression of old data should bring that number back down.
// For simple use cases or infrequent writes, this will last significantly longer.
#define BLOCKS_PER_EPOCH 65536

#define MAX_OPEN_EPOCH_FILES 8
#define BLOCK_ALIGNMENT 512

#define LONG_TIME_MILLIS (1000 * 3600)
#define WANTED_TX_PER_BLOCK 180
#define MAX_TX_PER_BLOCK 1024


struct blockchain_metadata_header_t {
	uint32_t version;
	uint64_t lastBlockTime;
	long currentBlockHeight;
};

struct blockchain_epoch_header_t {
	uint32_t version;
	uint32_t epoch;
	uint32_t blocksPerEpoch;
	uint32_t numBlocks;
	uint32_t blockAlignment;
	uint32_t blockOffset[BLOCKS_PER_EPOCH];
};

struct block_header_t {
	uint64_t millis;
	uint16_t numTransactions;
	uint8_t reserved[54];
};


BlockchainBackend::BlockchainBackend(Logger& logger, const std::string& dataDir)
	: dataDir(dataDir), openEpochs(32), log(logger, "BlockchainBackend") {
	FdHandle metadataHandle = FdHandle::open((dataDir + "/metadata.bin").c_str(), O_RDWR | O_CREAT, 0660);
	metadataFile = metadataHandle.getMmapHandle(0, sizeof(blockchain_metadata_header_t));
	header = metadataFile.directPointer<blockchain_metadata_header_t>();
	if (metadataHandle.isNew()) {
		*header = {CURRENT_FILE_VERSION, 0, 0};
	}
}

long BlockchainBackend::addBlock(const ArrayList<sp<Transaction>>& transactions) {
	uint64_t millis = millis_since_epoch();
	uint64_t duration = millis - header->lastBlockTime;

	// Skip timing checks if we already have the maximum per block - maximize throughput in this case
	if (transactions.size() != MAX_TX_PER_BLOCK) {
		// Basic checks: Has enough time elapsed since the last block?
		// Average block time should be 60 seconds.  Most systems will have ~180-240 transactions per minute.
		// Transaction rate varies, so block time should go down if there are too many transactions.
		// Thus, if the transaction queue has 180 transactions, the throughput will be 180 transactions per minute.
		//  Throughput = numTransactions / blockTime
		//  ThroughputWanted = numTransactions^2 / 180
		// Thus:
		//  blockTime = 60 * 180 / numTransactions
		// Block time should stay in the range of 5s to 600s.
		uint64_t blockTime = std::max(std::min(60 * 180 * 1000 / transactions.size(), 600 * 1000), 5 * 1000);
		if (duration < blockTime || (duration > LONG_TIME_MILLIS && transactions.size() > WANTED_TX_PER_BLOCK/2) || transactions.size() > MAX_TX_PER_BLOCK)
			// Not time to mine yet
				return -1;
	}

	// First, verify the transactions with a transient state
	// TODO: Implement that

	// Get general epoch file setup/info
	uint64_t blockNumber = ++header->currentBlockHeight;
	MmapHandle* epochFile = getEpochFile(blockNumber);
	blockchain_epoch_header_t* epochFileHeader = epochFile->directPointer<blockchain_epoch_header_t>(0);

	// Actually write the block to disk
	uint64_t blockOffset = epochFileHeader->blockOffset[blockNumber % BLOCKS_PER_EPOCH];
	block_header_t* blockHeader = epochFile->directPointer<block_header_t>(blockOffset);
	*blockHeader = {millis, (uint16_t)transactions.size(), {}};
	epochFile->seek(blockOffset + sizeof(block_header_t));
	for (const sp<Transaction>& transaction: transactions)
		transaction->write(epochFile);

	// Next write to the index
	// TODO: Write to index

	log.debug("New block mined with %u transactions at %u transactions per minute.", transactions.size(), transactions.size() * 1000 / duration);

	return (long)blockNumber;
}


long BlockchainBackend::getBlockHeight() const {
	return header->currentBlockHeight;
}

uint64_t BlockchainBackend::getLastBlockTimestamp() const {
	return header->lastBlockTime;
}

MmapHandle* BlockchainBackend::getEpochFile(uint64_t blockNumber) {
	uint32_t epochNumber = (uint32_t)(blockNumber / BLOCKS_PER_EPOCH);
	if (!openEpochs.hasKey(epochNumber)) {
		while (openEpochs.size() >= MAX_OPEN_EPOCH_FILES) {
			// Remove a random epoch file from open set
			int index = rand() % MAX_OPEN_EPOCH_FILES;
			if (openEpochs.presentAtIndex(index)) {
				delete openEpochs.remove(openEpochs.keyAtIndex(index));
				break;
			}
		}

		FdHandle epochFile = FdHandle::open((dataDir + "/epochs/" + std::to_string(epochNumber) + ".bin").c_str(), O_RDWR | O_CREAT);
		MmapHandle* epochMmap = new MmapHandle(epochFile.getMmapHandle(0, epochFile.seek(1024 * 1024, SEEK_END)));
		if (epochFile.isNew()) {
			// Initialize the file
			blockchain_epoch_header_t* epochHeader = epochMmap->directPointer<blockchain_epoch_header_t>();
			epochHeader->version = CURRENT_FILE_VERSION;
			epochHeader->blocksPerEpoch = BLOCKS_PER_EPOCH;
			epochHeader->epoch = epochNumber;
			epochHeader->numBlocks = 0;
			epochHeader->blockAlignment = BLOCK_ALIGNMENT;
			epochHeader->blockOffset[0] = (sizeof(blockchain_epoch_header_t) + BLOCK_ALIGNMENT) & ~BLOCK_ALIGNMENT;
		}

		openEpochs.put(epochNumber, epochMmap);
		return epochMmap;
	}

	return openEpochs.get(epochNumber);
}
