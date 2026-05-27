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
 
#ifndef LIBMAGELESSCHAIN_BLOCKTIMESTAMPTRACKER_H
#define LIBMAGELESSCHAIN_BLOCKTIMESTAMPTRACKER_H

#include <cstdint>
#include <string>
#include <mutex>

#include "ds/ArrayList.h"
#include "fs/BTree.h"

/**
 * Entry for the block timestamp BTree index.
 */
struct block_timestamp_entry_t {
	uint64_t timestamp;
	uint64_t blockNumber;

	static int compare(const block_timestamp_entry_t& a, const block_timestamp_entry_t& b) {
		if (a.timestamp < b.timestamp) return -1;
		if (a.timestamp > b.timestamp) return 1;
		if (a.blockNumber < b.blockNumber) return -1;
		if (a.blockNumber > b.blockNumber) return 1;
		return 0;
	}

	static block_timestamp_entry_t successor(block_timestamp_entry_t s) {
		if (s.blockNumber != UINT64_MAX) { s.blockNumber++; return s; }
		s.blockNumber = 0;
		if (s.timestamp != UINT64_MAX) { s.timestamp++; return s; }
		return s;
	}
};

/**
 * Manages a BTree index mapping timestamps to block numbers.
 */
class BlockTimestampTracker {
public:
	explicit BlockTimestampTracker(const std::string& dataDir);

	BlockTimestampTracker(const BlockTimestampTracker&) = delete;
	BlockTimestampTracker& operator=(const BlockTimestampTracker&) = delete;

	/**
	 * Record a new block's timestamp.
	 */
	void addBlock(uint64_t timestamp, uint64_t blockNumber);

	/**
	 * Find all block numbers in the given time window [startMillis, endMillis].
	 */
	ArrayList<uint64_t> getBlocksInWindow(uint64_t startMillis, uint64_t endMillis);

	~BlockTimestampTracker();

private:
	BTree<block_timestamp_entry_t>* index;
	std::mutex mutex;
};

#endif //LIBMAGELESSCHAIN_BLOCKTIMESTAMPTRACKER_H
