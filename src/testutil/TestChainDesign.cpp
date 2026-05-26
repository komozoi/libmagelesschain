
/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-23
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

#include "TestChainDesign.h"
#include "universaltime.h"

#include "storage/Catalog.h"
#include "storage/CatalogFile.h"

#include <cstring>

/*
 * latestSum/latestCount: range-scan the catalog for every catalog file
 * that may carry segments for this index, walk each file's BTree under
 * the read lock, pick the segment with the highest blockRangeEnd (last
 * mergeGeneration wins on tie), mmap its 8-byte payload through the
 * catalog file's reader, and decode.  No RAM-side caching: each query
 * touches catalog metadata and one mmap.
 */
static bool readLatestSegment(const BlockchainIndex& self, Catalog* cat, uint16_t indexId,
	int& outSum, int& outCount) {
	outSum = 0;
	outCount = 0;
	if (!cat) return false;

	ArrayList<uint64_t> fileIds = cat->rangeScan(indexId, 0, UINT64_MAX);
	if (fileIds.size() == 0) return false;

	uint16_t wantVersion = self.encodingVersion();
	bool found = false;
	uint64_t bestEnd = 0;
	uint32_t bestGen = 0;
	int bestSum = 0, bestCount = 0;

	for (int f = 0; f < fileIds.size(); ++f) {
		sp<CatalogFile> file = cat->getCatalogFile(fileIds.get(f));
		if (!file) continue;
		file.mut().openForReading<int>([&](CatalogFileReader& reader) {
			reader.forEachSegment(indexId, 0, UINT64_MAX,
				[&](const segment_btree_metadata_t& s) {
					if (s.encodingVersion != wantVersion) return;
					if (s.byteLength < 8) return;
					bool better = !found
						|| s.blockRangeEnd > bestEnd
						|| (s.blockRangeEnd == bestEnd && s.mergeGeneration > bestGen);
					if (!better) return;
					MmapHandle view = reader.openEntry(s.byteOffset, s.byteLength);
					const uint8_t* data = (const uint8_t*)view.directPointer<uint8_t>(view.seek(0, SEEK_CUR));
					if (!data) return;
					std::memcpy(&bestSum, data, 4);
					std::memcpy(&bestCount, data + 4, 4);
					bestEnd = s.blockRangeEnd;
					bestGen = s.mergeGeneration;
					found = true;
				});
			return 0;
		});
	}

	if (!found) return false;
	outSum = bestSum;
	outCount = bestCount;
	return true;
}

int TestSumIndex::latestSum() const {
	int sum = 0, count = 0;
	readLatestSegment(*this, attachedCatalog, attachedIndexId, sum, count);
	return sum;
}

int TestSumIndex::latestCount() const {
	int sum = 0, count = 0;
	readLatestSegment(*this, attachedCatalog, attachedIndexId, sum, count);
	return count;
}

Bytestring TestSumIndex::mergeSegments(const ArrayList<segment_coordinate_t>& inputs) const {
	// Absolute-state segments: simply re-hand the latest payload.  Real
	// indexes whose segments encode deltas would combine them here.  This
	// path is not exercised in Phase 1 since the new Catalog handles
	// compaction internally.
	(void)inputs;
	return Bytestring();
}

Bytestring TestSumOverrideFamily::seal() const {
	if (!dirty) return Bytestring();
	// Absolute new state = committed + pending delta.
	int absSum = (index ? index->latestSum() : 0) + sumDelta;
	int absCount = (index ? index->latestCount() : 0) + countDelta;
	uint8_t buf[8];
	std::memcpy(buf, &absSum, 4);
	std::memcpy(buf + 4, &absCount, 4);
	return Bytestring((void*)buf, 8);
}

TestTransaction::TestTransaction(int val, int id)
	: value(val), timestamp(millis_since_epoch()), id(id) {}

bool TestTransaction::verify(const StateOverride&) const {
	return true;
}

bool TestTransaction::apply(StateOverride& state) const {
	TestSumOverrideFamily& fam = state.override<TestSumOverrideFamily>(0);
	fam.sumDelta += value;
	fam.countDelta++;
	fam.dirty = true;
	return true;
}

float TestTransaction::computeValue(const StateOverride& state) const {
	const TestSumOverrideFamily& fam = state.override<TestSumOverrideFamily>(0);
	int curCount = fam.count();
	if (id == 2) {
		return (curCount > 0) ? 20.0f : 5.0f;
	}
	if (id == 1) {
		return 10.0f;
	}
	if (id == 3) {
		return (curCount == 0) ? 15.0f : 1.0f;
	}
	return (float)value;
}

uint8_t TestTransaction::getTypeId() const { return 1; }
uint64_t TestTransaction::getTimestamp() const { return timestamp; }

void TestTransaction::write(MmapHandle* dst) const {
	dst->write(getTypeId());
	dst->write(timestamp);
	dst->write(value);
	dst->write(id);
}

size_t TestTransaction::size() const {
	return sizeof(uint8_t) + sizeof(uint64_t) + sizeof(int) + sizeof(int);
}

sp<Transaction> TestTransaction::createFromMmap(MmapHandle* src) {
	uint64_t ts;
	int val;
	int id;
	src->read(ts);
	src->read(val);
	src->read(id);
	sp<TestTransaction> tx = sp<TestTransaction>::create(val, id);
	tx.mut().timestamp = ts;
	return tx;
}

void TestChainDesign::registerIndexes(BackendRegistry& registry) {
	registry.registerIndex<TestSumIndex>(sp<TestSumIndex>::create());
}

void TestChainDesign::registerOverrides(StateOverrideRegistry& registry) {
	registry.registerOverride<TestSumOverrideFamily>();
}

void TestChainDesign::registerTransactionTypes(TransactionTypeRegistry& registry) {
	registry.registerType(1, &TestTransaction::createFromMmap);
}
