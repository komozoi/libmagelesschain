
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

TestTransaction::TestTransaction(int val, int id)
	: value(val), timestamp(millis_since_epoch()), id(id) {}

bool TestTransaction::verify(const StateOverride&) const {
	return true;
}

bool TestTransaction::apply(StateOverride& state) const {
	TestSumOverrideFamily& fam = state.override<TestSumOverrideFamily>(0);
	fam.sum += value;
	fam.count++;
	return true;
}

float TestTransaction::computeValue(const StateOverride& state) const {
	const TestSumOverrideFamily& fam = state.override<TestSumOverrideFamily>(0);
	if (id == 2) {
		return (fam.count > 0) ? 20.0f : 5.0f;
	}
	if (id == 1) {
		return 10.0f;
	}
	if (id == 3) {
		return (fam.count == 0) ? 15.0f : 1.0f;
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
