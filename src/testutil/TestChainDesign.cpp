
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
#include "storage/IndexContainer.h"
#include "storage/IndexContainerManager.h"

#include <cstring>

/*
 * latestSum/latestCount: ask the catalog for every segment covering this
 * index instance, take the one with the highest blockRangeEnd (last
 * mergeGeneration wins on tie), mmap its 8-byte payload, and decode.  No
 * RAM-side caching: each query touches the catalog + one mmap.
 */
static bool readLatestSegment(const BlockchainIndex& self, Catalog* cat, IndexContainerManager* mgr,
		uint16_t typeId, uint8_t instanceId, int& outSum, int& outCount) {
	outSum = 0;
	outCount = 0;
	if (!cat || !mgr) return false;

	ArrayList<SegmentLocator> segs = cat->getAllSegments(typeId, instanceId);
	if (segs.size() == 0) return false;

	// Entries are sorted ascending by (blockStart, mergeGeneration).  We
	// want the latest committed state, i.e. the segment whose
	// blockRangeEnd is largest; on tie the highest mergeGeneration wins.
	int pickIdx = 0;
	for (int i = 1; i < segs.size(); ++i) {
		const SegmentLocator& a = segs.get(pickIdx);
		const SegmentLocator& b = segs.get(i);
		if (b.blockRangeEnd > a.blockRangeEnd
			|| (b.blockRangeEnd == a.blockRangeEnd && b.mergeGeneration > a.mergeGeneration)) {
			pickIdx = i;
		}
	}
	const SegmentLocator& pick = segs.get(pickIdx);
	if (pick.encodingVersion != self.encodingVersion()) return false;
	if (pick.byteLength < 8) return false;

	IndexContainer::PayloadView view = mgr->mmapPayload(pick.containerId, pick.byteOffset, pick.byteLength);
	if (!view.data) return false;
	std::memcpy(&outSum, view.data, 4);
	std::memcpy(&outCount, view.data + 4, 4);
	return true;
}

int TestSumIndex::latestSum() const {
	int sum = 0, count = 0;
	readLatestSegment(*this, attachedCatalog, attachedContainers, attachedPersistentTypeId, attachedInstanceId, sum, count);
	return sum;
}

int TestSumIndex::latestCount() const {
	int sum = 0, count = 0;
	readLatestSegment(*this, attachedCatalog, attachedContainers, attachedPersistentTypeId, attachedInstanceId, sum, count);
	return count;
}

Bytestring TestSumIndex::mergeSegments(const ArrayList<SegmentLocator>& inputs) const {
	// Absolute-state segments: the input with the highest blockRangeEnd
	// already represents the union of all the input block ranges, so we
	// just hand its payload back as the merged payload.  Indexes whose
	// payloads are deltas would actually combine them here.
	if (inputs.size() == 0) return Bytestring();
	if (!attachedContainers) return Bytestring();

	int pickIdx = 0;
	for (int i = 1; i < inputs.size(); ++i) {
		const SegmentLocator& a = inputs.get(pickIdx);
		const SegmentLocator& b = inputs.get(i);
		if (b.blockRangeEnd > a.blockRangeEnd
			|| (b.blockRangeEnd == a.blockRangeEnd && b.mergeGeneration > a.mergeGeneration)) {
			pickIdx = i;
		}
	}
	const SegmentLocator& pick = inputs.get(pickIdx);
	IndexContainer::PayloadView view = attachedContainers->mmapPayload(pick.containerId, pick.byteOffset, pick.byteLength);
	if (!view.data) return Bytestring();
	return Bytestring((void*)view.data, (size_t)view.length);
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
