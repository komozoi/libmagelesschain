/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-27
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

#include "EpochFile.h"


struct block_header_t {
	uint64_t millis;
	uint16_t numTransactions;
	uint8_t reserved[54];
};


EpochFile::EpochFile(const FdHandle& file)
	: mmapHandle(file.getMmapHandle(0, file.seek(1024 * 1024, SEEK_END))) {
	header = mmapHandle.directPointer<blockchain_epoch_header_t>(0);
	if (file.isNew()) {
		header->version = CURRENT_FILE_VERSION;
		header->blocksPerEpoch = BLOCKS_PER_EPOCH;
		header->numBlocks = 0;
		header->blockAlignment = BLOCK_ALIGNMENT;
		header->blockOffset[0] = (sizeof(blockchain_epoch_header_t) + BLOCK_ALIGNMENT - 1) & ~(BLOCK_ALIGNMENT - 1);
	}
}

uint32_t EpochFile::getBlockOffset(uint64_t blockNumber) const {
	return header->blockOffset[blockNumber % BLOCKS_PER_EPOCH];
}

void EpochFile::appendBlock(uint64_t blockNumber, uint64_t millis, const ArrayList<sp<Transaction>>& transactions) {
	uint32_t offsetIdx = (uint32_t)(blockNumber % BLOCKS_PER_EPOCH);
	uint32_t blockOffset = header->blockOffset[offsetIdx];
	if (blockOffset == 0) {
		blockOffset = (sizeof(blockchain_epoch_header_t) + BLOCK_ALIGNMENT - 1) & ~(BLOCK_ALIGNMENT - 1);
		header->blockOffset[offsetIdx] = blockOffset;
	}

	block_header_t* blockHeader = mmapHandle.directPointer<block_header_t>(blockOffset);
	*blockHeader = {millis, (uint16_t)transactions.size(), {}};
	mmapHandle.seek(blockOffset + sizeof(block_header_t));
	for (const sp<Transaction>& transaction: transactions)
		transaction->write(&mmapHandle);

	// Update offset for next block
	if ((blockNumber + 1) % BLOCKS_PER_EPOCH != 0) {
		uint64_t nextOffset = mmapHandle.seek(0, SEEK_CUR);
		header->blockOffset[(blockNumber + 1) % BLOCKS_PER_EPOCH] = (nextOffset + BLOCK_ALIGNMENT - 1) & ~(BLOCK_ALIGNMENT - 1);
	}
}

ArrayList<sp<Transaction>> EpochFile::readBlock(uint64_t blockNumber, const TransactionTypeRegistry& txTypes) {
	uint32_t blockOffset = getBlockOffset(blockNumber);
	if (blockOffset == 0) throw std::runtime_error("Block offset is zero");
	ArrayList<sp<Transaction>> results;
	block_header_t* blockHeader = mmapHandle.directPointer<block_header_t>(blockOffset);
	mmapHandle.seek(blockOffset + sizeof(block_header_t));
	for (int i = 0; i < blockHeader->numTransactions; ++i) {
		sp<Transaction> tx = txTypes.read(&mmapHandle);
		if (!tx)
			throw std::runtime_error("Failed to deserialize transaction");
		results.add(tx);
	}
	return results;
}
