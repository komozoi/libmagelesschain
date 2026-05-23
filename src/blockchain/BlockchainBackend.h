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

#include "Logger.h"
#include "Transaction.h"
#include "alloc/pointer.h"
#include "ds/ArrayList.h"
#include "ds/HashMap.h"
#include "fs/FdHandle.h"


struct blockchain_metadata_header_t;

/*
 * Backend to handle storage, validation, indexing, etc
 * of actual data.
 */
class BlockchainBackend {
public:
	explicit BlockchainBackend(Logger& logger, const std::string& dataDir);

	// Only one instance of this should probably exist at a time.
	BlockchainBackend(BlockchainBackend const&) = delete;
	BlockchainBackend(BlockchainBackend&&) = delete;

	MmapHandle* getEpochFile(uint64_t blockNumber);

	long addBlock(const ArrayList<sp<Transaction>>& transactions);

	long getBlockHeight() const;
	uint64_t getLastBlockTimestamp() const;

private:

	std::string dataDir;
	HashMap<uint32_t, MmapHandle*> openEpochs;
	MmapHandle metadataFile;
	blockchain_metadata_header_t* header;

	LogEndpoint log;
};


#endif //LIBMAGELESSCHAIN_BLOCKCHAINBACKEND_H