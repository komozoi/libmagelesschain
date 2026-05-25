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

#include "Catalog.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hash.h"

#define CATALOG_TOC_MAGIC   0xCA7A106Cu
#define CATALOG_TOC_VERSION 1u

struct catalog_toc_header_t {
	uint32_t magic;
	uint32_t version;
	uint32_t numEntries;
	uint32_t reserved;
};

Catalog::Catalog(const std::string& catalogDir)
	: catalogDir(catalogDir), filesDir(catalogDir + "/files"), tocPath(catalogDir + "/toc.bin"),
	  tocEntries(), slots(), maxFileId(0) {
	mkdir(catalogDir.c_str(), 0770);
	mkdir(filesDir.c_str(), 0770);
	load();
}

Catalog::~Catalog() {
	for (int i = 0; i < slots.size(); ++i) {
		delete slots.get(i);
	}
}

/*
 * NB: excessiveFastHash's `size` parameter counts 8-byte words, not
 * bytes, so we pack the bloom key into a uint64_t and pass size=1.
 */
static uint64_t bloomKeyHash(uint16_t persistentTypeId, uint8_t instanceId) {
	uint64_t key = ((uint64_t)persistentTypeId << 8) | (uint64_t)instanceId;
	return (uint64_t)excessiveFastHash(&key, 1);
}

void Catalog::bloomAdd(uint64_t bits[4], uint16_t persistentTypeId, uint8_t instanceId) {
	uint64_t h = bloomKeyHash(persistentTypeId, instanceId);
	uint64_t h1 = h & 0xFFFFFFFFull;
	uint64_t h2 = (h >> 32) | 1ull;
	for (int k = 0; k < 3; ++k) {
		uint32_t bit = (uint32_t)((h1 + (uint64_t)k * h2) & 0xFFu);
		bits[bit >> 6] |= (1ull << (bit & 63));
	}
}

bool Catalog::bloomMaybeContains(const uint64_t bits[4], uint16_t persistentTypeId, uint8_t instanceId) {
	uint64_t h = bloomKeyHash(persistentTypeId, instanceId);
	uint64_t h1 = h & 0xFFFFFFFFull;
	uint64_t h2 = (h >> 32) | 1ull;
	for (int k = 0; k < 3; ++k) {
		uint32_t bit = (uint32_t)((h1 + (uint64_t)k * h2) & 0xFFu);
		if ((bits[bit >> 6] & (1ull << (bit & 63))) == 0)
			return false;
	}
	return true;
}

uint64_t Catalog::blockRangeDistance(const CatalogFileEntry& tocEntry, uint64_t segStart, uint64_t segEnd) {
	if (tocEntry.segmentCount == 0) return 0;
	if (segEnd < tocEntry.blockRangeMin)
		return tocEntry.blockRangeMin - segEnd;
	if (segStart > tocEntry.blockRangeMax)
		return segStart - tocEntry.blockRangeMax;
	return 0; // overlapping or adjacent
}

void Catalog::load() {
	// Load TOC.  An empty TOC is fine; we just start with no files.
	FdHandle tocFile = FdHandle::open(tocPath.c_str(), O_RDWR | O_CREAT, 0660);
	off_t end = tocFile.seek(0, SEEK_END);
	if (end < (off_t)sizeof(catalog_toc_header_t)) {
		// New file, nothing to load.
		return;
	}

	tocFile.seek(0, SEEK_SET);
	catalog_toc_header_t hdr;
	tocFile.read(hdr);
	if (hdr.magic != CATALOG_TOC_MAGIC) {
		// Corrupt or foreign file; treat as empty (the design says
		// catalogs are rebuildable from the journal).
		return;
	}

	for (uint32_t i = 0; i < hdr.numEntries; ++i) {
		CatalogFileEntry e;
		tocFile.read(e);
		tocEntries.add(e);
		if (e.fileId > maxFileId) maxFileId = e.fileId;

		FileSlot* slot = new FileSlot();
		slot->fileId = e.fileId;
		std::string path = filesDir + "/" + std::to_string(e.fileId) + ".bin";
		slot->file = FdHandle::open(path.c_str(), O_RDWR | O_CREAT, 0660);

		// Load every locator record in this file.
		FdHandle& f = slot->file;
		off_t fend = f.seek(0, SEEK_END);
		f.seek(0, SEEK_SET);
		size_t numRecs = (size_t)fend / sizeof(SegmentLocator);
		for (size_t j = 0; j < numRecs; ++j) {
			SegmentLocator loc;
			f.read(loc);
			slot->entries.add(loc);
		}

		// Sort by SegmentLocator::compare.  Insertion sort: the file is
		// append-only between rewrites, and compaction may interleave
		// merge-output entries with newer block-range entries, so disk
		// order is not the desired in-RAM order.
		for (int j = 1; j < slot->entries.size(); ++j) {
			SegmentLocator cur = slot->entries.get(j);
			int k = j - 1;
			while (k >= 0 && SegmentLocator::compare(slot->entries.get(k), cur) > 0) {
				slot->entries.get(k + 1) = slot->entries.get(k);
				--k;
			}
			slot->entries.get(k + 1) = cur;
		}

		slots.add(slot);
	}
}

void Catalog::rewriteToc() {
	// Caller holds tocMutex.
	std::string tmp = tocPath + ".tmp";
	::unlink(tmp.c_str());
	FdHandle tocFile = FdHandle::open(tmp.c_str(), O_RDWR | O_CREAT, 0660);
	catalog_toc_header_t hdr = {CATALOG_TOC_MAGIC, CATALOG_TOC_VERSION, (uint32_t)tocEntries.size(), 0};
	tocFile.seek(0, SEEK_SET);
	tocFile.write(hdr);
	for (int i = 0; i < tocEntries.size(); ++i) {
		tocFile.write(tocEntries.get(i));
	}
	tocFile = FdHandle();
	::rename(tmp.c_str(), tocPath.c_str());
}

uint64_t Catalog::createFile() {
	// Caller holds tocMutex.
	maxFileId++;
	uint64_t newId = maxFileId;

	FileSlot* slot = new FileSlot();
	slot->fileId = newId;
	std::string path = filesDir + "/" + std::to_string(newId) + ".bin";
	slot->file = FdHandle::open(path.c_str(), O_RDWR | O_CREAT, 0660);

	CatalogFileEntry entry = {};
	entry.fileId = newId;
	entry.blockRangeMin = UINT64_MAX;
	entry.blockRangeMax = 0;
	entry.totalBytes = 0;
	entry.segmentCount = 0;

	tocEntries.add(entry);
	slots.add(slot);
	return newId;
}

uint64_t Catalog::selectFileFor(const SegmentLocator& entry) const {
	// Caller holds tocMutex.  Returns 0 if a new file should be created.
	long bestIdx = -1;
	// Score:  +100 for bloom hit, minus block-range distance scaled down.
	// Higher is better.
	long bestScore = LONG_MIN;

	for (int i = 0; i < tocEntries.size(); ++i) {
		const CatalogFileEntry& e = tocEntries.get(i);
		if (e.totalBytes + entry.byteLength > CATALOG_FILE_MAX_BYTES)
			continue;
		long score = 0;
		if (bloomMaybeContains(e.bloomBits, entry.persistentTypeId, entry.instanceId))
			score += 100;
		uint64_t dist = blockRangeDistance(e, entry.blockRangeStart, entry.blockRangeEnd);
		// Scale block-range distance into a comparable penalty.  Distance
		// is in blocks; we cap the penalty at -99 so a bloom hit always
		// wins over the best non-bloom candidate.
		long penalty = (dist > 0) ? -(long)((dist > 99 ? 99 : dist)) : 0;
		score += penalty;
		// Prefer earlier (smaller fileId) on tie so packing stays tight.
		score -= (long)i;
		if (score > bestScore) {
			bestScore = score;
			bestIdx = i;
		}
	}

	if (bestIdx < 0) return 0;
	return tocEntries.get((int)bestIdx).fileId;
}

void Catalog::appendToFile(FileSlot& slot, const SegmentLocator& entry) {
	// Caller holds slot.fileMutex.  Append-only on disk; the in-RAM list
	// is sorted-inserted by the caller (insert()).
	slot.file.seek(0, SEEK_END);
	slot.file.write(entry);
}

void Catalog::rewriteFile(FileSlot& slot) {
	// Caller holds slot.fileMutex.  FdHandle has no truncate, so we drop
	// the fd, unlink, and reopen.  Then dump the in-RAM sorted list.
	std::string path = filesDir + "/" + std::to_string(slot.fileId) + ".bin";
	slot.file = FdHandle();
	::unlink(path.c_str());
	slot.file = FdHandle::open(path.c_str(), O_RDWR | O_CREAT, 0660);
	for (int i = 0; i < slot.entries.size(); ++i) {
		slot.file.write(slot.entries.get(i));
	}
}

void Catalog::insert(const SegmentLocator& entry) {
	uint64_t chosenFileId = 0;
	FileSlot* chosenSlot = nullptr;

	{
		std::lock_guard _(tocMutex);
		chosenFileId = selectFileFor(entry);
		if (chosenFileId == 0)
			chosenFileId = createFile();

		// Resolve slot pointer.  Slots and tocEntries are kept in sync.
		for (int i = 0; i < slots.size(); ++i) {
			if (slots.get(i)->fileId == chosenFileId) {
				chosenSlot = slots.get(i);
				break;
			}
		}
	}

	// Take the per-file mutex.  Other inserts that resolve to *other*
	// files can proceed in parallel here.
	{
		std::lock_guard _(chosenSlot->fileMutex);

		// Sorted insert into the in-RAM list.
		FileSlot& slot = *chosenSlot;
		int insertAt = slot.entries.size();
		for (int i = 0; i < slot.entries.size(); ++i) {
			if (SegmentLocator::compare(entry, slot.entries.get(i)) < 0) {
				insertAt = i;
				break;
			}
		}
		SegmentLocator filler = {};
		slot.entries.add(filler);
		for (int i = slot.entries.size() - 1; i > insertAt; --i)
			slot.entries.get(i) = slot.entries.get(i - 1);
		slot.entries.get(insertAt) = entry;

		appendToFile(slot, entry);
	}

	// Update TOC stats + bloom.
	{
		std::lock_guard _(tocMutex);
		for (int i = 0; i < tocEntries.size(); ++i) {
			if (tocEntries.get(i).fileId == chosenFileId) {
				CatalogFileEntry& e = tocEntries.get(i);
				bloomAdd(e.bloomBits, entry.persistentTypeId, entry.instanceId);
				if (entry.blockRangeStart < e.blockRangeMin) e.blockRangeMin = entry.blockRangeStart;
				if (entry.blockRangeEnd > e.blockRangeMax)   e.blockRangeMax = entry.blockRangeEnd;
				e.totalBytes += entry.byteLength;
				e.segmentCount++;
				break;
			}
		}
		rewriteToc();
	}
}

int Catalog::findOwningSlot(const SegmentLocator& entry) const {
	// Caller holds tocMutex.  Use bloom + block range as a fast filter;
	// then walk that file's list (under fileMutex acquired by caller).
	for (int i = 0; i < tocEntries.size(); ++i) {
		const CatalogFileEntry& e = tocEntries.get(i);
		if (e.segmentCount == 0) continue;
		if (!bloomMaybeContains(e.bloomBits, entry.persistentTypeId, entry.instanceId))
			continue;
		if (entry.blockRangeStart > e.blockRangeMax) continue;
		if (entry.blockRangeEnd < e.blockRangeMin)   continue;

		// Scan the file's entries (lock-free read of an immutable slot
		// vector is safe; entries themselves need the file's lock for
		// concurrent inserts elsewhere -- but findOwningSlot only runs
		// under tocMutex so insert()'s TOC update is serialized with us).
		const FileSlot& slot = *slots.get(i);
		for (int j = 0; j < slot.entries.size(); ++j) {
			const SegmentLocator& s = slot.entries.get(j);
			if (s.persistentTypeId == entry.persistentTypeId
				&& s.instanceId == entry.instanceId
				&& s.blockRangeStart == entry.blockRangeStart
				&& s.blockRangeEnd == entry.blockRangeEnd
				&& s.mergeGeneration == entry.mergeGeneration) {
				return i;
			}
		}
	}
	return -1;
}

void Catalog::remove(const SegmentLocator& entry) {
	std::lock_guard _(tocMutex);

	int idx = findOwningSlot(entry);
	if (idx < 0) return;

	FileSlot* slotPtr = slots.get(idx);
	{
		std::lock_guard _(slotPtr->fileMutex);
		FileSlot& slot = *slotPtr;

		int found = -1;
		for (int j = 0; j < slot.entries.size(); ++j) {
			const SegmentLocator& s = slot.entries.get(j);
			if (s.persistentTypeId == entry.persistentTypeId
				&& s.instanceId == entry.instanceId
				&& s.blockRangeStart == entry.blockRangeStart
				&& s.blockRangeEnd == entry.blockRangeEnd
				&& s.mergeGeneration == entry.mergeGeneration) {
				found = j;
				break;
			}
		}
		if (found < 0) return;

		// Shift left and pop.
		for (int j = found; j < slot.entries.size() - 1; ++j)
			slot.entries.get(j) = slot.entries.get(j + 1);
		slot.entries.pop();

		rewriteFile(slot);

		// Recompute TOC stats for the slot.  Block range bounds may
		// shrink; bloom does not shrink (false positives are accepted).
		CatalogFileEntry& te = tocEntries.get(idx);
		te.segmentCount = (uint32_t)slot.entries.size();
		te.totalBytes = (te.totalBytes >= entry.byteLength) ? te.totalBytes - entry.byteLength : 0;
		if (slot.entries.size() == 0) {
			te.blockRangeMin = UINT64_MAX;
			te.blockRangeMax = 0;
		} else {
			uint64_t mn = UINT64_MAX, mx = 0;
			for (int j = 0; j < slot.entries.size(); ++j) {
				const SegmentLocator& s = slot.entries.get(j);
				if (s.blockRangeStart < mn) mn = s.blockRangeStart;
				if (s.blockRangeEnd > mx)   mx = s.blockRangeEnd;
			}
			te.blockRangeMin = mn;
			te.blockRangeMax = mx;
		}
	}

	rewriteToc();
}

ArrayList<SegmentLocator> Catalog::rangeScan(uint16_t persistentTypeId, uint8_t instanceId,
	uint64_t startBlock, uint64_t endBlock) const {

	// Snapshot the candidate slot pointers under tocMutex, then release
	// the TOC mutex while we range-scan their per-file lists.
	ArrayList<FileSlot*> candidates;
	{
		std::lock_guard _(tocMutex);
		for (int i = 0; i < tocEntries.size(); ++i) {
			const CatalogFileEntry& e = tocEntries.get(i);
			if (e.segmentCount == 0) continue;
			if (!bloomMaybeContains(e.bloomBits, persistentTypeId, instanceId)) continue;
			if (startBlock > e.blockRangeMax) continue;
			if (endBlock < e.blockRangeMin) continue;
			candidates.add(slots.get(i));
		}
	}

	ArrayList<SegmentLocator> out;
	for (int i = 0; i < candidates.size(); ++i) {
		FileSlot* slotPtr = candidates.get(i);
		std::lock_guard _(slotPtr->fileMutex);
		const FileSlot& slot = *slotPtr;
		for (int j = 0; j < slot.entries.size(); ++j) {
			const SegmentLocator& s = slot.entries.get(j);
			if (s.persistentTypeId != persistentTypeId || s.instanceId != instanceId) continue;
			if (s.blockRangeStart > endBlock) continue;
			if (s.blockRangeEnd < startBlock) continue;
			out.add(s);
		}
	}

	// Sort merged results so callers see ascending (typeId, instId,
	// blockRangeStart, mergeGeneration) order independent of which
	// catalog file each hit came from.  Insertion sort is fine here:
	// rangeScan result sets are typically tiny.
	for (int i = 1; i < out.size(); ++i) {
		SegmentLocator cur = out.get(i);
		int j = i - 1;
		while (j >= 0 && SegmentLocator::compare(out.get(j), cur) > 0) {
			out.get(j + 1) = out.get(j);
			--j;
		}
		out.get(j + 1) = cur;
	}

	return out;
}

int Catalog::countSegments(uint16_t persistentTypeId, uint8_t instanceId) const {
	return getAllSegments(persistentTypeId, instanceId).size();
}

ArrayList<SegmentLocator> Catalog::getAllSegments(uint16_t persistentTypeId, uint8_t instanceId) const {
	return rangeScan(persistentTypeId, instanceId, 0, UINT64_MAX);
}

int Catalog::catalogFileCount() const {
	std::lock_guard _(tocMutex);
	return tocEntries.size();
}
