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

#include <climits>
#include <sys/stat.h>

#include <bigint.h>
#include <ds/Bytestring.h>
#include <ds/PriorityQueue.h>
#include <parallel/ThreadPool.h>

#include "CatalogFile.h"


static uint256_t bloomKeyHash(uint16_t indexId) {
	uint64_t miniHash = (uint64_t)indexId * 15879638260049341073UL + 5277654339219660263UL;
	uint64_t hash1 = miniHash % 2404975667;
	uint64_t hash2 = miniHash % 3414420799;

	uint256_t output;
	output.setBit(hash1 & 255);
	output.setBit(hash2 & 255);

	return output;
}

static bool bloomMaybeContains(const uint256_t& bits, uint16_t indexId) {
	uint256_t hash = bloomKeyHash(indexId);
	return (bits & hash) == hash;
}

static std::string idToFilename(uint64_t id) {
	return std::to_string(id) + ".bin";
}

uint64_t blockRangeDistance(const catalog_contents_entry_t& e, uint64_t segStart, uint64_t segEnd) {
	if (e.segmentCount == 0) return 0;
	if (segEnd < e.blockRangeMin) return e.blockRangeMin - segEnd;
	if (segStart > e.blockRangeMax) return segStart - e.blockRangeMax;
	return 0;
}

int catalog_contents_entry_t::compare(const catalog_contents_entry_t& a, const catalog_contents_entry_t& b) {
	// Sort by the reverse of the file ID
	// This allows us to find files by ID and iterate from new to old
	// New files are likely more interesting for putting new segments into, for example,
	// or for querying the latest data.

	if (a.fileId < b.fileId) return 1;
	if (a.fileId > b.fileId) return -1;
	return 0;
}

static sp<CatalogFile> buildCatalogFile(const FdHandle& fd) {
	return sp<CatalogFile>::create(fd);
}

/*
 * Construct or reopen the catalog rooted at `catalogDir`.  Creates the
 * directory tree if needed, opens the TOC BTree, and reopens every
 * catalog file the TOC references.  No replay of segments happens here
 * (the per-file BTrees stay on disk and are queried lazily).
 */
Catalog::Catalog(const std::string& catalogDir, ThreadPool& executor)
	: catalogDir(catalogDir), catalogCache(catalogDir, buildCatalogFile), executor(executor), filesBeingWritten(64) {
	// Deliberately ignore errors.
	mkdir(catalogDir.c_str(), 0770);

	FdHandle tocFile = FdHandle::open((catalogDir + "/toc.bin").c_str(), O_RDWR | O_CREAT, 0660);
	tocBTree = new BTree<catalog_contents_entry_t, 31>(std::move(tocFile), 0, catalog_contents_entry_t::compare);
}


sp<CatalogFile> Catalog::getCatalogFile(uint64_t fileId) {
	return catalogCache.open(idToFilename(fileId));
}


void Catalog::writeSegment(uint16_t indexId, uint16_t version, uint16_t mergeGeneration, uint64_t startBlock, uint64_t endBlock, Bytestring&& content) {
	catalog_contents_entry_t key;
	sp<CatalogFile> file = selectFileFor(indexId, startBlock, endBlock, content.size(), key);
	if (!file)
		throw std::runtime_error("Unable to find a catalog file to write a segment too.  Errno: " + std::string(strerror(errno)));

	{
		std::unique_lock _(beingWrittenMutex);
		filesBeingWritten.add(file.get());
	}

	executor.submit([this, indexId, version, mergeGeneration, startBlock, endBlock, content, file, key]() mutable {
		try {
			file.mut().createEntry(indexId, version, mergeGeneration, startBlock, endBlock, content);

			std::unique_lock _(tocMutex);
			tocBTree->find(key); // Refresh key with latest data from TOC
			key.totalBytes = file->getTotalBytes();
			key.blockRangeMin = std::min(key.blockRangeMin, startBlock);
			key.blockRangeMax = std::max(key.blockRangeMax, endBlock);
			key.bloom = key.bloom | bloomKeyHash(indexId);
			key.segmentCount++;
			tocBTree->overwrite(key);
		} catch (const std::exception& rethrown) {
			std::unique_lock _(beingWrittenMutex);
			filesBeingWritten.remove(file.get());
			throw;
		}

		std::unique_lock _(beingWrittenMutex);
		filesBeingWritten.remove(file.get());
	});
}

ArrayList<uint64_t> Catalog::rangeScan(uint16_t indexId, uint64_t startBlock, uint64_t endBlock) {
	std::shared_lock _(tocMutex);
	ArrayList<uint64_t> out;

	uint256_t indexBloom = bloomKeyHash(indexId);

	// Search starting with the newest catalog files to oldest
	catalog_contents_entry_t key = {millis_since_epoch(), 0, 0, 0, 0, 0, {}};
	while (tocBTree->findNext(key)) {
		if ((indexBloom & key.bloom) == indexBloom && key.blockRangeMin <= endBlock && key.blockRangeMax >= startBlock)
			out.add(key.fileId);

		key.fileId--;
	}

	return out;
}

sp<CatalogFile> Catalog::selectFileFor(uint16_t indexId, uint64_t startBlock, uint64_t endBlock, uint32_t size, catalog_contents_entry_t& key) {
	{
		std::shared_lock _(tocMutex);

		// Collect up to 16 candidates to write to
		StaticPriorityQueue<uint64_t, 16> candidates;

		// Search starting with the newest catalog files to oldest
		// Stop when we have enough candidates or we've reached the end of the catalog
		key = {millis_since_epoch(), 0, 0, 0, 0, 0, {}};
		while (tocBTree->findNext(key) && candidates.length() < 16) {
			if (key.totalBytes + size <= CATALOG_FILE_MAX_BYTES) {
				// This score prioritizes files that are more isolated to a few indexes, meaning queries are more
				// efficient.  This is due to likelyhood of each segment belonging to the correct index
				int score = 256 - key.bloom.countBits();
				if (bloomMaybeContains(key.bloom, indexId))
					score += 100;

				// Try to find a file that is close to the desired block range
				score -= (int)sqrt((double)blockRangeDistance(key, startBlock, endBlock)) / 10;

				// Look for files that have less segments, so queries are likely more efficient
				score -= key.segmentCount;

				candidates.add(key.fileId, score);
			}

			key.fileId--;
		}

		candidates.sort();
		std::shared_lock _2(beingWrittenMutex);
		for (int i = 0; i < candidates.length(); ++i) {
			sp<CatalogFile> file = catalogCache.open(idToFilename(candidates.at(i)));

			key.fileId = candidates.at(i);
			if (!tocBTree->find(key))
				throw std::runtime_error("Catalog file not found in BTree, despite supposedly being indexed (SEVERE BUG, REPORT THIS!)");

			if (file && !filesBeingWritten.contains(file.get()))
				return file;
		}
	}

	// If we got here, then we haven't found a suitable file.  We'll need to create one.

	uint64_t newId = millis_since_epoch();
	{
		std::unique_lock _(tocMutex);
		if (newId <= lastFileId)
			newId++;
		lastFileId = newId;
		key = {newId, UINT64_MAX, 0, 0, 0, 0, {}};
	}

	return catalogCache.open(idToFilename(newId), O_RDWR | O_CREAT);
}

Catalog::~Catalog() {
	delete tocBTree;
}
