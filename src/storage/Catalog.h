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

#include <cstdint>
#include <string>

#include "SegmentLocator.h"

#include "fs/FdHandle.h"
#include "ds/ArrayList.h"
#include "alloc/pointer.h"

/*
 * The catalog records the location and metadata of every segment in the
 * chain.  Phase 2 implementation is a single catalog file backed by an
 * in-memory ArrayList kept sorted by SegmentLocator::compare so range
 * scans return segments in ascending (blockRangeStart, mergeGeneration)
 * order.  The concrete proposal (§12) describes a Table of Contents +
 * per-file catalog scheme with a 256-bit bloom-style bitmask; the public
 * API below is stable regardless: callers should only rely on the
 * range-scan semantics, not on whether one or many physical catalog files
 * back them.
 *
 * There is no "obsolete" state.  When compaction replaces N input
 * segments with one merged output, the inputs are removed from the
 * catalog entirely (via remove()) and their disk regions are freed back
 * to the container's FreeSpaceFile.  An old segment is part of the index
 * for as long as it lives in the catalog; once removed it is gone.
 *
 * The catalog file is *not* fsync'd.  Per §12.6 the journal is the
 * source of truth; the catalog can always be rebuilt from it.
 *
 * TODO: Implement proper catalog per the proposal, using BTrees
 */
class Catalog {
public:
	explicit Catalog(const std::string& catalogDir);

	/*
	 * Insert a new segment entry.  Appended to the on-disk log and kept
	 * sorted in memory.
	 */
	void insert(const SegmentLocator& entry);

	/*
	 * Remove a segment entry.  Drops it from the in-memory list and
	 * rewrites the on-disk log so subsequent reopens do not see it.  The
	 * disk region of the segment's payload must be released by the caller
	 * via the container manager; the catalog does not own that space.
	 */
	void remove(const SegmentLocator& entry);

	/*
	 * Range scan: return every segment for the given index instance whose
	 * [blockRangeStart, blockRangeEnd] intersects [startBlock, endBlock]
	 * (inclusive both sides).  Returned in ascending
	 * (blockRangeStart, mergeGeneration) order.
	 */
	ArrayList<SegmentLocator> rangeScan(uint16_t persistentTypeId, uint8_t instanceId, uint64_t startBlock, uint64_t endBlock) const;

	/*
	 * Count of segments for an index instance.
	 */
	int countSegments(uint16_t persistentTypeId, uint8_t instanceId) const;

	/*
	 * Return all segments for an index instance, ascending
	 * (blockRangeStart, mergeGeneration).
	 */
	ArrayList<SegmentLocator> getAllSegments(uint16_t persistentTypeId, uint8_t instanceId) const;

private:
	void loadFromFile();
	void rewriteFile();

	std::string catalogDir;
	/*
	 * In-memory list of all known segments, kept sorted ascending by
	 * (persistentTypeId, instanceId, blockRangeStart, mergeGeneration) so
	 * range scans can short-circuit.  The companion file `catalog.bin` is
	 * append-only on insert and rewritten on remove.  On construction the
	 * file is replayed into memory.  This design intentionally trades a
	 * BTree for an ArrayList while the catalog stays small enough to fit
	 * in RAM (segment counts grow only logarithmically with chain size
	 * due to compaction); the public API is unchanged so a BTree-backed
	 * implementation can be swapped in later without disturbing callers.
	 */
	ArrayList<SegmentLocator> entries;
	mutable FdHandle catalogFile;
};

#endif //LIBMAGELESSCHAIN_CATALOG_H
