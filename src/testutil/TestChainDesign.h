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

#ifndef LIBMAGELESSCHAIN_TESTCHAINDESIGN_H
#define LIBMAGELESSCHAIN_TESTCHAINDESIGN_H

#include "blockchain/ChainDesign.h"
#include "blockchain/BlockchainIndex.h"
#include "blockchain/IndexOverride.h"
#include "blockchain/BackendRegistry.h"
#include "blockchain/StateOverrideRegistry.h"
#include "blockchain/StateOverride.h"
#include "blockchain/Transaction.h"
#include "blockchain/TransactionTypeRegistry.h"


/*
 * Simple "global sum" index used by the test suite.
 *
 * Conceptually this is the committed materialized state for the sum-state
 * test chain.  Phase 1 storage is in-memory; once segment storage lands the
 * sum will be persisted via BlockchainIndex::writeSegment.
 */
class TestSumIndex : public BlockchainIndex {
public:
	int sum = 0;
	int count = 0;

	uint16_t encodingVersion() const override { return 0; }
	Bytestring writeSegment(uint64_t, uint64_t) const override { return Bytestring(); }
	bool readSegment(const Bytestring&, uint16_t, uint64_t, uint64_t) override { return true; }
	Bytestring mergeSegments(const ArrayList<Bytestring>&, const ArrayList<uint16_t>&) const override {
		return Bytestring();
	}
};

/*
 * Override family paired with TestSumIndex.  Stores the pending sum/count
 * delta accumulated by transactions in the block-in-progress.  Must be
 * copy-constructible so MEVBuilder candidate forking via sp<T> CoW works.
 */
class TestSumOverrideFamily : public IndexOverrideFamilyBase {
public:
	int sum = 0;
	int count = 0;
};

/*
 * Test transaction adding `value` to the sum and bumping count.  `id` selects
 * an alternate value-scoring rule so MEV order-dependency tests can verify
 * that computeValue is re-evaluated against the simulated state after each
 * pick.
 */
class TestTransaction : public Transaction {
public:
	int value;
	uint64_t timestamp;
	int id;

	TestTransaction(int val = 1, int id = 0);

	bool verify(const StateOverride& state) const override;
	bool apply(StateOverride& state) const override;
	float computeValue(const StateOverride& state) const override;

	uint8_t getTypeId() const override;
	uint64_t getTimestamp() const override;

	void write(MmapHandle* dst) const override;

	size_t size() const override;

	static sp<Transaction> createFromMmap(MmapHandle* src);
};

/*
 * ChainDesign used by the test suite.  Registers one TestSumIndex, one
 * matching TestSumOverrideFamily, and the TestTransaction type.
 */
class TestChainDesign : public ChainDesign {
public:
	void registerIndexes(BackendRegistry& registry) override;
	void registerOverrides(StateOverrideRegistry& registry) override;
	void registerTransactionTypes(TransactionTypeRegistry& registry) override;
};

#endif //LIBMAGELESSCHAIN_TESTCHAINDESIGN_H
