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

#include <gtest/gtest.h>
#include <filesystem>

#include "blockchain/MEVBuilder.h"
#include "blockchain/BlockchainBackend.h"
#include "blockchain/StateOverride.h"
#include "testutil/TestChainDesign.h"
#include "universaltime.h"


class MEVBuilderTest : public ::testing::Test {
protected:
	std::string testDir;
	Logger logger;

	MEVBuilderTest() : logger("cmake-build-debug/test_logs", 0, 0) {}

	sp<ChainDesign> makeDesign() {
		return sp<ChainDesign>(sp<TestChainDesign>::create());
	}

	void SetUp() override {
		uint64_t seconds = millis_since_epoch() / 1000;
		std::string testName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
		testDir = "cmake-build-debug/test_data/" + std::to_string(seconds) + "-" + testName;
		std::filesystem::create_directories(testDir);
	}

	void TearDown() override {
		if (std::filesystem::exists(testDir)) {
			std::filesystem::remove_all(testDir);
		}
	}
};

TEST_F(MEVBuilderTest, BasicSelection) {
	BlockchainBackend backend(logger, testDir, makeDesign());
	MEVBuilder builder(backend);

	ArrayList<sp<Transaction>> mempool;
	mempool.add(sp<TestTransaction>::create(1));
	mempool.add(sp<TestTransaction>::create(5));
	mempool.add(sp<TestTransaction>::create(3));

	ArrayList<sp<Transaction>> block = builder.buildBlock(mempool, 2, 0);

	EXPECT_EQ(block.size(), 2);
	EXPECT_EQ(((TestTransaction&)*block.get(0)).value, 5);
	EXPECT_EQ(((TestTransaction&)*block.get(1)).value, 3);
	EXPECT_EQ(mempool.size(), 1);
	EXPECT_EQ(((TestTransaction&)*mempool.get(0)).value, 1);
}

TEST_F(MEVBuilderTest, RespectMaxTransactions) {
	BlockchainBackend backend(logger, testDir, makeDesign());
	MEVBuilder builder(backend);

	ArrayList<sp<Transaction>> mempool;
	for (int i = 0; i < 10; ++i) {
		mempool.add(sp<TestTransaction>::create(i));
	}

	ArrayList<sp<Transaction>> block = builder.buildBlock(mempool, 5, 0);

	EXPECT_EQ(block.size(), 5);
	EXPECT_EQ(mempool.size(), 5);
	EXPECT_EQ(((TestTransaction&)*block.get(0)).value, 9);
	EXPECT_EQ(((TestTransaction&)*block.get(4)).value, 5);
}

TEST_F(MEVBuilderTest, EmptyMempool) {
	BlockchainBackend backend(logger, testDir, makeDesign());
	MEVBuilder builder(backend);

	ArrayList<sp<Transaction>> mempool;
	ArrayList<sp<Transaction>> block = builder.buildBlock(mempool, 10, 0);

	EXPECT_EQ(block.size(), 0);
	EXPECT_EQ(mempool.size(), 0);
}

TEST_F(MEVBuilderTest, ComplexExecutionOrderDependency) {
	BlockchainBackend backend(logger, testDir, makeDesign());
	MEVBuilder builder(backend);

	ArrayList<sp<Transaction>> mempool;
	// Tx A: id=1, value 10
	// Tx B: id=2, value 5 initially, 20 if count > 0
	// Tx C: id=3, value 15 initially, 1 if count > 0

	mempool.add(sp<TestTransaction>::create(0, 1));
	mempool.add(sp<TestTransaction>::create(0, 2));
	mempool.add(sp<TestTransaction>::create(0, 3));

	// Iterative logic with maxTransactions = 2:
	// 1. Initial values: 1=10, 2=5, 3=15.  Pick id=3 (15).
	// 2. count becomes 1.
	// 3. Re-evaluate: 1=10, 2=20.  Pick id=2 (20).
	ArrayList<sp<Transaction>> block = builder.buildBlock(mempool, 2, 0);
	EXPECT_EQ(block.size(), 2);
	EXPECT_EQ(((TestTransaction&)*block.get(0)).id, 3);
	EXPECT_EQ(((TestTransaction&)*block.get(1)).id, 2);

	// Verify remaining in mempool
	EXPECT_EQ(mempool.size(), 1);
	EXPECT_EQ(((TestTransaction&)*mempool.get(0)).id, 1);
}

TEST_F(MEVBuilderTest, StartsFromBackendNotFrontendState) {
	// The builder must work from a fresh backend StateOverride; tests pin
	// this by mutating an unrelated override copy and ensuring the
	// builder's selection is unaffected.
	BlockchainBackend backend(logger, testDir, makeDesign());
	MEVBuilder builder(backend);

	// Independently mutate a separate override; the builder should not
	// see this when scoring transactions.
	sp<StateOverride> stray = backend.newStateOverride();
	stray.mut().override<TestSumOverrideFamily>(0).countDelta = 100;

	ArrayList<sp<Transaction>> mempool;
	mempool.add(sp<TestTransaction>::create(0, 3));  // id=3: 15 if count==0 else 1
	mempool.add(sp<TestTransaction>::create(0, 1));  // id=1: always 10

	// With a fresh backend state, id=3 starts at 15 so it should win over id=1.
	ArrayList<sp<Transaction>> block = builder.buildBlock(mempool, 1, 0);
	ASSERT_EQ(block.size(), 1);
	EXPECT_EQ(((TestTransaction&)*block.get(0)).id, 3);
}

TEST_F(MEVBuilderTest, ExecutionOrderDependency) {
	BlockchainBackend backend(logger, testDir, makeDesign());
	MEVBuilder builder(backend);

	ArrayList<sp<Transaction>> mempool;
	mempool.add(sp<TestTransaction>::create(0, 2));
	mempool.add(sp<TestTransaction>::create(0, 1));

	// 1. id=2 (val 5), id=1 (val 10).  Pick id=1.
	// 2. Apply id=1.  count becomes 1.
	// 3. Re-evaluate id=2 -> now 20.  Pick id=2.
	ArrayList<sp<Transaction>> block = builder.buildBlock(mempool, 10, 0);
	EXPECT_EQ(block.size(), 2);
	EXPECT_EQ(((TestTransaction&)*block.get(0)).id, 1);
	EXPECT_EQ(((TestTransaction&)*block.get(1)).id, 2);
}
