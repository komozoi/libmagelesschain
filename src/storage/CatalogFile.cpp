
/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-25
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

#include "CatalogFile.h"

#include <hash.h>
#include <stdexcept>
#include <ds/Bytestring.h>



MmapHandle CatalogFileReader::openEntry(uint64_t offset, uint64_t size) const {
	return fd.getMmapHandle(offset, size, PROT_READ);
}



CatalogFile::CatalogFile(const FdHandle &fd)
	: fd(fd), regions(fd) {

	segmentIndex = new BTree(fd, regions.getHeaderEnd(), segment_btree_metadata_t::compare);
}


void CatalogFile::createEntry(uint16_t indexId, uint16_t version, uint16_t mergeGeneration, uint64_t startBlock, uint64_t endBlock, const Bytestring& content) {
	off_t offset;
	uint32_t length = content.size();

	{
		// It is not safe to read the regions BTree while we're updating it.
		std::unique_lock _(appendMutex);
		offset = regions.getFreeRegion(length);
	}

	// Check that a free region was successfully allocated
	if (offset < 0)
		throw std::runtime_error("CatalogFile::createEntry: failed to allocate file region");

	{
		// For this part of the method, it is safe to share the mutex because we're not altering existing on-disk data
		// that would be read.  We're only writing data to a region of the file, which is not referenced elsewhere yet.
		std::shared_lock _(rwMutex);
		fd.queueWrite(&content[0], length, offset);
	}

	// Prepare to add the entry to the segment index
	uint64_t checksum = excessiveFastHash(&content[0], length);
	segment_btree_metadata_t metadata = {startBlock, endBlock, (uint64_t)offset, length, checksum, mergeGeneration, indexId, version};

	{
		// Here we need to prevent reading because we cannot read and write to the BTree at the same time
		// There is also an edge case where the two BTrees race to allocate space at the end of the file,
		// causing corruption.  For this reason, it is not safe to add data to two both BTrees at the same time.
		std::unique_lock _1(rwMutex);
		std::unique_lock _2(appendMutex);
		segmentIndex->insert(metadata);
	}
}

void CatalogFile::deleteEntry(uint16_t indexId, uint64_t startBlock, uint16_t mergeGeneration) {
	segment_btree_metadata_t toRemove = {startBlock, 0, 0, 0, 0, mergeGeneration, indexId, 0};

	{
		// Safely remove from internal index first
		std::unique_lock _(rwMutex);
		segmentIndex->remove(toRemove);
	}

	{
		// Now free so the region can be reused
		std::unique_lock _(appendMutex);
		regions.markFreeRegion(toRemove.byteOffset, toRemove.byteLength);
	}
}