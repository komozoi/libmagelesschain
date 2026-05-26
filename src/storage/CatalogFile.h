
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

#ifndef LIBMAGELESSCHAIN_CATALOGFILE_H
#define LIBMAGELESSCHAIN_CATALOGFILE_H

#include <atomic>
#include <functional>
#include <shared_mutex>
#include <fs/FreeSpaceFile.h>


/*
 * Catalog entry describing the location and metadata of a single on-disk
 * segment.  A segment is the unit of persistent index state: one segment
 * per (index instance, block range) when the override family for that
 * index produced non-empty pending changes for the committed block.
 *
 * The struct is a fixed-size POD so it can be stored directly as the
 * element type of libexcessive's BTree, which requires a fixed sizeof for
 * its node layout.
 *
 * Persistence of the type identifier:
 *   `persistentTypeId` is assigned by the backend at construction time
 *   from the registration order of indexes within the active ChainDesign.
 *   The same order must be reproduced across restarts (this is the same
 *   registration-order contract that backs backend.index<T>(id)).  This
 *   is intentionally not the in-process TypeKey<T>() pointer, which is
 *   not stable across processes.
 */
struct segment_btree_metadata_t {
	uint64_t blockRangeStart;    // inclusive
	uint64_t blockRangeEnd;      // exclusive
	uint64_t byteOffset;
	uint64_t byteLength;
	uint64_t checksum;
	uint32_t mergeGeneration;
	uint16_t indexId;
	uint16_t encodingVersion;

	/*
	 * Lexicographic ordering by (persistentTypeId, instanceId,
	 * blockRangeStart, mergeGeneration).  `tombstone` is intentionally
	 * not part of the key so find()/overwrite() can locate a live entry
	 * by its key and flip the tombstone bit in place.  Including
	 * mergeGeneration last keeps merge-output entries adjacent to the
	 * inputs they replace and lets a range scan see them deterministically.
	 */
	static int compare(const segment_btree_metadata_t& a, const segment_btree_metadata_t& b) {
		if (a.indexId < b.indexId) return -1;
		if (a.indexId > b.indexId) return 1;
		if (a.blockRangeStart < b.blockRangeStart) return -1;
		if (a.blockRangeStart > b.blockRangeStart) return 1;
		if (a.mergeGeneration < b.mergeGeneration) return -1;
		if (a.mergeGeneration > b.mergeGeneration) return 1;
		return 0;
	}

	/*
	 * Successor in the key ordering used by compare().  Returns the
	 * smallest SegmentLocator strictly greater than `s` in that
	 * ordering.  Used to advance through a BTree<SegmentLocator> via
	 * repeated findNext() calls.
	 *
	 * Wraps cleanly through the key fields without ever returning the
	 * same key; if `s` is at the maximum, returns `s` unchanged with
	 * its tombstone untouched (callers iterate until findNext returns
	 * false, so this edge case is harmless).
	 */
	static segment_btree_metadata_t successor(segment_btree_metadata_t s) {
		if (s.mergeGeneration != UINT32_MAX) { s.mergeGeneration++; return s; }
		s.mergeGeneration = 0;
		if (s.blockRangeStart != UINT64_MAX) { s.blockRangeStart++; return s; }
		s.blockRangeStart = 0;
		if (s.indexId != UINT16_MAX) { s.indexId++; return s; }
		return s;
	}
};


class Bytestring;


class CatalogFileReader {
public:
	CatalogFileReader(const FdHandle& fd, BTree<segment_btree_metadata_t>* segmentIndex) : fd(fd), segmentIndex(segmentIndex) {}

	MmapHandle openEntry(uint64_t offset, uint64_t size) const;

	/*
	 * Walk every segment of `indexId` whose [blockRangeStart, blockRangeEnd]
	 * intersects [startBlock, endBlock] (inclusive), in ascending
	 * (blockRangeStart, mergeGeneration) order.  Caller must hold the read
	 * lock through openForReading() so the BTree stays stable.
	 */
	void forEachSegment(uint16_t indexId, uint64_t startBlock, uint64_t endBlock, const std::function<void(const segment_btree_metadata_t&)>& callback) const;

private:
	FdHandle fd;
	BTree<segment_btree_metadata_t>* segmentIndex;
};


/**
 * Represents and provides access to a catalog file.  Ensures all operations are thread-safe.
 *
 * It should be impossible to use this unsafely.
 */
class CatalogFile {
public:
	explicit CatalogFile(const FdHandle& fd);

	// A callback is used to ensure that the read lock is held for the entire time the Mmap handle is open
	template<typename T>
	T openForReading(const std::function<T(CatalogFileReader& reader)>& callback) {
		std::shared_lock _(rwMutex);

		CatalogFileReader reader(fd, segmentIndex);
		return callback(reader);
	}

	void createEntry(uint16_t indexId, uint16_t version, uint16_t mergeGeneration, uint64_t startBlock, uint64_t endBlock, const Bytestring& content);
	void deleteEntry(uint16_t indexId, uint64_t startBlock, uint16_t mergeGeneration);

	bool isBusy() const { return isSegmentBeingWritten.load(); }

	~CatalogFile() {
		delete segmentIndex;
	}

	uint64_t getTotalBytes() const;

private:
	FdHandle fd;
	FreeSpaceFile regions;
	BTree<segment_btree_metadata_t>* segmentIndex;

	std::shared_mutex rwMutex;
	std::shared_mutex appendMutex;

	std::atomic<bool> isSegmentBeingWritten = false;
};


#endif //LIBMAGELESSCHAIN_CATALOGFILE_H
