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

#include "BlockTimestampTracker.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

BlockTimestampTracker::BlockTimestampTracker(const std::string& dataDir) {
	std::string path = dataDir + "/timestamps.bin";
	FdHandle fd = FdHandle::open(path.c_str(), O_RDWR | O_CREAT, 0660);
	index = new BTree<block_timestamp_entry_t>(std::move(fd), 0, block_timestamp_entry_t::compare);
}

void BlockTimestampTracker::addBlock(uint64_t timestamp, uint64_t blockNumber) {
	std::lock_guard _(mutex);
	block_timestamp_entry_t entry = {timestamp, blockNumber};
	index->insert(entry);
}

ArrayList<uint64_t> BlockTimestampTracker::getBlocksInWindow(uint64_t startMillis, uint64_t endMillis) {
	std::lock_guard _(mutex);
	ArrayList<uint64_t> results;
	
	// findNext finds the given value if present, or the next highest value.
	// We start with the smallest possible block number for the start timestamp.
	block_timestamp_entry_t cursor = {startMillis, 0};
	
	while (index->findNext(cursor)) {
		if (cursor.timestamp > endMillis) {
			break;
		}
		results.add(cursor.blockNumber);
		cursor = block_timestamp_entry_t::successor(cursor);
	}
	
	return results;
}

BlockTimestampTracker::~BlockTimestampTracker() {
	delete index;
}
