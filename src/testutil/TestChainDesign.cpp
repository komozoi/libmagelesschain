
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

TestState::TestState(BlockchainBackend& backend, long blockHeight)
	: BlockchainStateSnapshot(backend, blockHeight) {}

TestTransaction::TestTransaction(int val)
	: value(val), timestamp(millis_since_epoch()) {}

bool TestTransaction::verify(BlockchainStateSnapshot&) const {
	return true;
}

bool TestTransaction::apply(BlockchainStateSnapshot& snapshot) const {
	TestState& s = (TestState&)snapshot;
	s.sum += value;
	return true;
}

float TestTransaction::computeValue(BlockchainStateSnapshot&) const {
	return 1.0f;
}

uint8_t TestTransaction::getTypeId() const {
	return 1;
}

uint64_t TestTransaction::getTimestamp() const {
	return timestamp;
}

void TestTransaction::write(MmapHandle* dst) const {
	dst->write(getTypeId());
	dst->write(timestamp);
	dst->write(value);
}

size_t TestTransaction::size() const {
	return sizeof(uint8_t) + sizeof(uint64_t) + sizeof(int);
}

sp<Transaction> TestTransaction::createFromMmap(MmapHandle* src) {
	uint64_t ts;
	int val;
	src->read(ts);
	src->read(val);
	sp<TestTransaction> tx = sp<TestTransaction>::create(val);
	tx.mut().timestamp = ts;
	return tx;
}
