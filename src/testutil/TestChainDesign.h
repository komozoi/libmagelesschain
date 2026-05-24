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
 * The committed state is stored entirely on disk as a sequence of
 * absolute-state segments (one segment per block that recorded changes).
 * latestSum() / latestCount() query the catalog for the most recent
 * segment via the attached storage and mmap its 8-byte payload.  No
 * cached in-RAM state.
 */
class TestSumIndex : public BlockchainIndex {
public:
	uint16_t encodingVersion() const override { return 1; }
	Bytestring mergeSegments(const ArrayList<SegmentLocator>& inputs) const override;

	int latestSum() const;
	int latestCount() const;
};

/*
 * Override family paired with TestSumIndex.  Holds the pending delta to
 * apply on top of the committed sum/count.  attach() wires up a pointer
 * to the matching TestSumIndex so reads through to committed state are
 * cheap (one catalog scan + one mmap per query).
 *
 * Must be copy-constructible so MEVBuilder candidate forking via sp<T>
 * CoW works.
 */
class TestSumOverrideFamily : public IndexOverrideFamilyBase {
public:
	int sumDelta = 0;
	int countDelta = 0;
	bool dirty = false;
	TestSumIndex* index = nullptr;

	void attach(BlockchainIndex& idx) override { index = (TestSumIndex*)&idx; }

	/* Read-through accessors: committed state + pending delta. */
	int sum() const { return (index ? index->latestSum() : 0) + sumDelta; }
	int count() const { return (index ? index->latestCount() : 0) + countDelta; }

	Bytestring seal() const override;
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
