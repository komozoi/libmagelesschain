/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-24
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

#ifndef LIBMAGELESSCHAIN_CATALOG_H
#define LIBMAGELESSCHAIN_CATALOG_H

#include <bigint.h>
#include <LongKey.h>
#include <mutex>
#include <shared_mutex>
#include <string>

#include "CustomizableFileCache.h"

#include "fs/BTree.h"
#include "ds/ArrayList.h"

/*
 * Per-file capacity ceiling for a catalog file.  An insert chooses a
 * catalog file such that file.totalBytes + locator.byteLength stays
 * <= CATALOG_FILE_MAX_BYTES; otherwise a new file is allocated.
 *
 * Kept just under 2 GiB so a catalog file roughly corresponds to one
 * container's worth of segments and stays well below the 2 GiB Java
 * compatibility threshold.
 */
constexpr uint64_t CATALOG_FILE_MAX_BYTES = 2047ull * 1024ull * 1024ull;


/*
 * Catalog file metadata as stored in the TOC BTree.  One per catalog
 * file.  Cached per-file stats (bloom, block range, totals) live here
 * so we never have to open the file to decide whether it is a candidate
 * for an insert or a query.
 */
struct catalog_contents_entry_t {
	uint64_t fileId;             // primary key
	uint64_t blockRangeMin;      // UINT64_MAX if empty
	uint64_t blockRangeMax;      // 0 if empty
	uint64_t totalBytes;         // sum of live SegmentLocator::byteLength
	uint256_t bloom;          // 256-bit bloom over (typeId, instanceId)
	uint32_t segmentCount;       // live (non-tombstoned) segment count
	uint8_t  reserved[4];

	static int compare(const catalog_contents_entry_t& a, const catalog_contents_entry_t& b);
};


class ThreadPool;
class CatalogFile;
class Bytestring;

/*
 * Multi-file catalog backed by libexcessive's BTree at every level.
 *
 *   <catalogDir>/toc.bin        BTree<catalog_contents_entry_t> keyed by fileId
 *   <catalogDir>/<id>.bin       BTree<segment_btree_metadata_t> keyed by
 *                               (typeId, instanceId, blockRangeStart,
 *                                mergeGeneration); one per catalog file
 *
 * The TOC's BTree records per-file metadata: covered block range, total
 * catalogued payload bytes, segment count, and a 256-bit bloom-style
 * bitmask over the (persistentTypeId, instanceId) pairs of segments
 * present in that file.  At query time the TOC is iterated via
 * BTree::findNext (ascending fileId), survivors are filtered by bloom
 * and block-range overlap, and only the relevant per-file BTrees are
 * range-scanned.  At insert time the TOC is iterated the same way to
 * pick the best-locality file under the capacity cap.
 *
 * Segments are never removed from a BTree (libexcessive's BTree::remove
 * only handles leaf nodes); compaction marks segments with the
 * SegmentLocator::tombstone bit and the BTree entry is rewritten via
 * BTree::overwrite.  Range scans skip tombstones.  The catalog's
 * `remove()` does the same.
 *
 * Each catalog file has its own mutex so two parallel writers operating
 * on different files do not contend; concurrent inserts that route to
 * the same file serialize on its file mutex.  The TOC has its own mutex
 * for slot lookup / file allocation and is held only briefly.
 *
 * Neither the TOC nor any catalog file is fsync'd.
 * The journal is the source of truth and catalog files are rebuildable.
 */
class Catalog {
public:
	explicit Catalog(const std::string& catalogDir, ThreadPool& executor);

	Catalog(const Catalog&) = delete;
	Catalog& operator=(const Catalog&) = delete;

	sp<CatalogFile> getCatalogFile(uint64_t fileId);

	/**
	 * Schedules segment write in the thread pool.  Automatically finds a decent catalog to write to, or creates a
	 * new one if needed.
	 *
	 * @param indexId Index ID of the segment
	 * @param version Index version writing this segment
	 * @param mergeGeneration Merge generation of this segment
	 * @param startBlock The first block indexed by this segment
	 * @param endBlock Block after the last block indexed by this segment
	 * @param content Raw segment bytes to store
	 */
	void writeSegment(uint16_t indexId, uint16_t version, uint16_t mergeGeneration, uint64_t startBlock, uint64_t endBlock, const Bytestring& content);

	/**
	 * Find all catalog files whose block ranges overlap with the given range, and which may have the requested index ID.
	 *
	 * @param indexId Index ID to filter for
	 * @param startBlock
	 * @param endBlock
	 * @return
	 */
	ArrayList<uint64_t> rangeScan(uint16_t indexId, uint64_t startBlock, uint64_t endBlock);

	~Catalog();

private:

	/*
	 * Pick a destination catalog file for `entry` by iterating the
	 * TOC BTree.
	 *
	 * Loads the file metadata for the selected file into `key`.
	 *
	 * Will create a file if needed or if other catalog files are busy.
	 */
	sp<CatalogFile> selectFileFor(uint16_t indexId, uint64_t startBlock, uint64_t endBlock, uint32_t size, catalog_contents_entry_t& key);

	std::string catalogDir;

	std::shared_mutex tocMutex;
	BTree<catalog_contents_entry_t, 31>* tocBTree;

	// This is used to ensure time-based filenames are not duplicated.
	uint64_t lastFileId;

	CustomizableFileCache<CatalogFile> catalogCache;
	ThreadPool& executor;
};

#endif //LIBMAGELESSCHAIN_CATALOG_H
