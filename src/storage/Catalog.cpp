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

#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

Catalog::Catalog(const std::string& catalogDir)
	: catalogDir(catalogDir), entries() {
	mkdir(catalogDir.c_str(), 0770);

	catalogFile = FdHandle::open((catalogDir + "/catalog.bin").c_str(), O_RDWR | O_CREAT, 0660);

	loadFromFile();
}

void Catalog::loadFromFile() {
	off_t end = catalogFile.seek(0, SEEK_END);
	catalogFile.seek(0, SEEK_SET);
	size_t numEntries = (size_t)end / sizeof(SegmentLocator);
	for (size_t i = 0; i < numEntries; ++i) {
		SegmentLocator loc;
		catalogFile.read(loc);
		entries.add(loc);
	}
	// Inserts append chronologically; in-memory entries must be sorted by
	// SegmentLocator::compare so range scans return segments in ascending
	// (blockRangeStart, mergeGeneration) order.  Post-compaction
	// merged-output segments are chronologically newer but logically older,
	// so the on-disk order is not the desired in-memory order.
	// Insertion-sort over the already-loaded list is tiny and correct.
	for (int i = 1; i < entries.size(); ++i) {
		SegmentLocator cur = entries.get(i);
		int j = i - 1;
		while (j >= 0 && SegmentLocator::compare(entries.get(j), cur) > 0) {
			entries.get(j + 1) = entries.get(j);
			--j;
		}
		entries.get(j + 1) = cur;
	}
}

void Catalog::rewriteFile() {
	// Rewrite the on-disk catalog from scratch.  Called only by remove(),
	// which is rare in steady state since it only fires when compaction
	// merges multiple segments together.  FdHandle does not expose
	// truncate, so we drop the current fd, unlink the file, and reopen.
	std::string path = catalogDir + "/catalog.bin";
	catalogFile = FdHandle();
	::unlink(path.c_str());
	catalogFile = FdHandle::open(path.c_str(), O_RDWR | O_CREAT, 0660);
	for (int i = 0; i < entries.size(); ++i) {
		catalogFile.write(entries.get(i));
	}
}

void Catalog::insert(const SegmentLocator& entry) {
	// Find insertion point to keep entries sorted by SegmentLocator::compare.
	// Catalog inserts are infrequent (~one per index per block, then batched
	// merges), so a linear scan is acceptable for now and trivially correct.
	int insertAt = entries.size();
	for (int i = 0; i < entries.size(); ++i) {
		if (SegmentLocator::compare(entry, entries.get(i)) < 0) {
			insertAt = i;
			break;
		}
	}

	// Shift right by one to make room.
	SegmentLocator filler = {};
	entries.add(filler);
	for (int i = entries.size() - 1; i > insertAt; --i) {
		entries.get(i) = entries.get(i - 1);
	}
	entries.get(insertAt) = entry;

	// Append to disk so the catalog survives restart.
	catalogFile.seek(0, SEEK_END);
	catalogFile.write(entry);
}

void Catalog::remove(const SegmentLocator& entry) {
	int found = -1;
	for (int i = 0; i < entries.size(); ++i) {
		const SegmentLocator& e = entries.get(i);
		if (e.persistentTypeId == entry.persistentTypeId
			&& e.instanceId == entry.instanceId
			&& e.blockRangeStart == entry.blockRangeStart
			&& e.blockRangeEnd == entry.blockRangeEnd
			&& e.mergeGeneration == entry.mergeGeneration) {
			found = i;
			break;
		}
	}
	if (found < 0) return;

	// Shift the tail left by one to keep the list contiguous and sorted.
	for (int i = found; i < entries.size() - 1; ++i) {
		entries.get(i) = entries.get(i + 1);
	}
	entries.pop();
	rewriteFile();
}

ArrayList<SegmentLocator> Catalog::rangeScan(uint16_t persistentTypeId, uint8_t instanceId, uint64_t startBlock, uint64_t endBlock) const {
	ArrayList<SegmentLocator> out;
	for (int i = 0; i < entries.size(); ++i) {
		const SegmentLocator& e = entries.get(i);
		if (e.persistentTypeId != persistentTypeId || e.instanceId != instanceId)
			continue;
		if (e.blockRangeStart > endBlock)
			continue;
		if (e.blockRangeEnd < startBlock)
			continue;
		out.add(e);
	}
	return out;
}

int Catalog::countSegments(uint16_t persistentTypeId, uint8_t instanceId) const {
	int count = 0;
	for (int i = 0; i < entries.size(); ++i) {
		const SegmentLocator& e = entries.get(i);
		if (e.persistentTypeId != persistentTypeId || e.instanceId != instanceId)
			continue;
		count++;
	}
	return count;
}

ArrayList<SegmentLocator> Catalog::getAllSegments(uint16_t persistentTypeId, uint8_t instanceId) const {
	return rangeScan(persistentTypeId, instanceId, 0, UINT64_MAX);
}
