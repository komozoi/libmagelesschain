/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-26
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

#ifndef LIBMAGELESSCHAIN_EPOCHFILE_H
#define LIBMAGELESSCHAIN_EPOCHFILE_H


#include <string>
#include "fs/FdHandle.h"

#include "Transaction.h"
#include "TransactionTypeRegistry.h"
#include "alloc/pointer.h"
#include "ds/ArrayList.h"

#define BLOCKS_PER_EPOCH 65536
#define BLOCK_ALIGNMENT 4096
#define CURRENT_FILE_VERSION 0xC7000000


struct blockchain_epoch_header_t {
	uint32_t version;
	uint32_t blocksPerEpoch;
	uint32_t numBlocks;
	uint32_t blockAlignment;
	uint32_t blockOffset[BLOCKS_PER_EPOCH];
};

class EpochFile {
public:
	EpochFile(const FdHandle& file);
	
	void appendBlock(uint64_t blockNumber, uint64_t millis, const ArrayList<sp<Transaction>>& transactions);

	ArrayList<sp<Transaction>> readBlock(uint64_t blockNumber, const TransactionTypeRegistry& txTypes);

private:
	uint32_t getBlockOffset(uint64_t blockNumber) const;

	MmapHandle mmapHandle;
	blockchain_epoch_header_t* header;
};

#endif
