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
#include <mutex>
#include <string>

#include "SegmentLocator.h"

#include "fs/FdHandle.h"
#include "ds/ArrayList.h"
#include "alloc/pointer.h"
#include "LongKey.h"

/*
 * Per-file capacity ceiling for a catalog file.  An insert chooses a
 * catalog file such that file.totalBytes + locator.byteLength stays
 * <= CATALOG_FILE_MAX_BYTES; otherwise a new file is allocated.
 *
 * This is the *payload* size accounted for, not the catalog file's own
 * size on disk.  It matches the per-container size cap so a catalog
 * file roughly corresponds to one container's worth of segments and
 * keeps both layers' working sets in the same order of magnitude.
 */
constexpr uint64_t CATALOG_FILE_MAX_BYTES = 2047ull * 1024ull * 1024ull;

/*
 * Multi-file catalog: a Table-Of-Contents file `toc.bin` and a directory
 * of per-range catalog files `files/<id>.bin`.  Each catalog file holds
 * an append-only log of SegmentLocator records and is kept sorted in
 * RAM by SegmentLocator::compare so range scans short-circuit.  Each
 * catalog file has its own mutex so a multi-threaded writer may route
 * concurrent inserts that resolve to different files in parallel.
 *
 * The TOC keeps one fixed-size record per catalog file with:
 *   - fileId
 *   - block range covered by the segments inside (min start, max end)
 *   - total payload bytes (the sum of locator.byteLength) which is what
 *     the per-file cap is enforced against
 *   - segment count
 *   - a 256-bit bloom-style bitmask over the (persistentTypeId,
 *     instanceId) pairs of segments in the file
 *
 * On insert, the file with the best locality score is chosen, provided
 * its post-insert totalBytes fits under CATALOG_FILE_MAX_BYTES.  The
 * scoring prefers files that already contain the same (typeId, instId)
 * (bloom hit) and whose existing block range is closest to the new
 * segment's block range, which keeps queries cheap by clustering
 * related segments together on disk.  If no existing file qualifies, a
 * new one is allocated.
 *
 * Neither the TOC nor any catalog file is fsync'd: per the design,
 * the journal is the source of truth and catalogs are rebuildable.
 *
 * TODO: Stop using linear search and use BTree instead.
 */
class Catalog {
public:
	explicit Catalog(const std::string& catalogDir);
	~Catalog();

	Catalog(const Catalog&) = delete;
	Catalog& operator=(const Catalog&) = delete;

	/*
	 * Insert a new segment entry into the catalog.  Picks an existing
	 * file based on capacity + locality, or creates a new file if no
	 * existing one qualifies.  Appends to the chosen file's on-disk log
	 * under the file's mutex, and updates the TOC entry under the TOC
	 * mutex.  Concurrent inserts that route to different files run in
	 * parallel.
	 */
	void insert(const SegmentLocator& entry);

	/*
	 * Remove a segment entry.  Drops it from the owning file's sorted
	 * list and rewrites that file's on-disk log so subsequent reopens
	 * do not see it.  Updates the TOC.  The disk region of the
	 * segment's payload must be released by the caller via the
	 * container manager.
	 */
	void remove(const SegmentLocator& entry);

	/*
	 * Range scan: return every segment for the given index instance
	 * whose [blockRangeStart, blockRangeEnd] intersects [startBlock,
	 * endBlock] (inclusive).  Filters candidate catalog files by their
	 * TOC bloom + block range overlap, then scans each surviving file.
	 * Returned in ascending (blockRangeStart, mergeGeneration) order.
	 */
	ArrayList<SegmentLocator> rangeScan(uint16_t persistentTypeId, uint8_t instanceId,
		uint64_t startBlock, uint64_t endBlock) const;

	/*
	 * Count segments for an index instance (full chain range).
	 */
	int countSegments(uint16_t persistentTypeId, uint8_t instanceId) const;

	/*
	 * All segments for an index instance, ascending
	 * (blockRangeStart, mergeGeneration).
	 */
	ArrayList<SegmentLocator> getAllSegments(uint16_t persistentTypeId, uint8_t instanceId) const;

	/*
	 * Number of catalog files currently tracked in the TOC.  Useful for
	 * tests that want to verify rollover behavior.
	 */
	int catalogFileCount() const;

private:
	/*
	 * In-RAM TOC entry; one per catalog file.  This is exactly what is
	 * persisted to `toc.bin` after the header.
	 */
	struct CatalogFileEntry {
		uint64_t fileId;
		uint64_t blockRangeMin;     // min blockRangeStart of segments in file; UINT64_MAX if empty
		uint64_t blockRangeMax;     // max blockRangeEnd of segments in file; 0 if empty
		uint64_t totalBytes;        // sum of byteLength across segments
		uint32_t segmentCount;
		uint32_t reserved;          // align to 8
		uint64_t bloomBits[4];      // 256-bit bloom over (typeId, instanceId)
	};

	/*
	 * Per-catalog-file slot.  Each slot owns the file handle, the
	 * in-RAM sorted segment list, and its own mutex.  Slots live inside
	 * sp<> so the TOC vector can grow without invalidating references
	 * held by inserts currently in flight on other slots.
	 */
	struct FileSlot {
		uint64_t fileId;
		mutable std::mutex fileMutex;
		ArrayList<SegmentLocator> entries;
		FdHandle file;

		FileSlot() = default;
		FileSlot(const FileSlot&) = delete;
		FileSlot& operator=(const FileSlot&) = delete;
	};

	/*
	 * 256-bit bloom helpers.  k=3 bit positions are derived from
	 * excessiveFastHash over the packed (typeId, instanceId) key.
	 */
	static void bloomAdd(uint64_t bits[4], uint16_t persistentTypeId, uint8_t instanceId);
	static bool bloomMaybeContains(const uint64_t bits[4], uint16_t persistentTypeId, uint8_t instanceId);

	/*
	 * Distance from a segment's block range to a catalog file's existing
	 * block range.  Lower is better; an empty file returns 0.
	 */
	static uint64_t blockRangeDistance(const CatalogFileEntry& tocEntry, uint64_t segStart, uint64_t segEnd);

	/*
	 * Pick a destination catalog file id for `entry`.  Returns 0 if a
	 * new file should be allocated; otherwise the id of an existing
	 * file.  Caller holds `tocMutex`.
	 */
	uint64_t selectFileFor(const SegmentLocator& entry) const;

	/*
	 * Allocate a brand-new catalog file (TOC entry + on-disk file).
	 * Caller holds `tocMutex`.  Returns the new fileId.
	 */
	uint64_t createFile();

	/*
	 * Load TOC + every catalog file into memory.  Called from the
	 * constructor.
	 */
	void load();

	/*
	 * Rewrite the TOC file from the in-RAM tocEntries.  Called when
	 * stats change.  TOC writes are cheap (one record per catalog
	 * file).  Caller holds `tocMutex`.
	 */
	void rewriteToc();

	/*
	 * Append one SegmentLocator to a catalog file.  Caller holds
	 * `slot.fileMutex`.
	 */
	static void appendToFile(FileSlot& slot, const SegmentLocator& entry);

	/*
	 * Replace a catalog file's contents with its in-RAM sorted list.
	 * Used on remove().  Caller holds `slot.fileMutex`.
	 */
	void rewriteFile(FileSlot& slot);

	/*
	 * Locate the FileSlot that owns the given entry, or return -1 if
	 * none does.  Caller holds `tocMutex` (read-locked, but this class
	 * uses a single mutex so any caller is the only TOC reader).
	 */
	int findOwningSlot(const SegmentLocator& entry) const;

	std::string catalogDir;
	std::string filesDir;
	std::string tocPath;

	mutable std::mutex tocMutex;
	/*
	 * TOC entries are kept in parallel with the slots vector.  Index i
	 * in `tocEntries` describes the file at `slots[i]`.
	 */
	ArrayList<CatalogFileEntry> tocEntries;
	/*
	 * FileSlots are heap-allocated and accessed via raw pointers so the
	 * slot's identity is stable across ArrayList growth (and so we can
	 * mutate the slot in place without sp<T>'s COW machinery detaching
	 * a private copy).  Owned by this Catalog; deleted in ~Catalog.
	 */
	ArrayList<FileSlot*> slots;
	uint64_t maxFileId;
};

#endif //LIBMAGELESSCHAIN_CATALOG_H
