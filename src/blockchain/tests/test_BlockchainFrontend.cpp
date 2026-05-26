/*
 * Copyright 2023-2025 komozoi
 * Original Creation Date: 2026-5-22
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
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
#include <unistd.h>

#include "blockchain/BlockchainBackend.h"
#include "blockchain/BlockchainFrontend.h"
#include "blockchain/BlockchainConfig.h"
#include "blockchain/StateOverride.h"
#include "blockchain/Transaction.h"
#include "universaltime.h"

#include "testutil/TestChainDesign.h"


class BlockchainFrontendTest : public ::testing::Test {
protected:
	std::string testDir;
	Logger logger;

	BlockchainFrontendTest() : logger("cmake-build-debug/test_logs", 0, 0) {}

	sp<ChainDesign> makeDesign() {
		return sp<ChainDesign>(sp<TestChainDesign>::create());
	}

	void SetUp() override {
		uint64_t seconds = millis_since_epoch() / 1000;
		std::string testName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
		testDir = "test_data/" + std::to_string(seconds) + "-" + testName;
		std::filesystem::create_directories(testDir);
		std::filesystem::create_directories(testDir + "/epochs");
	}

	void TearDown() override {
		if (!testDir.empty() && std::filesystem::exists(testDir)) {
			std::filesystem::remove_all(testDir);
		}
	}
};

static int currentSum(const BlockchainFrontend& frontend) {
	return frontend.getState()->override<TestSumOverrideFamily>(0).sum();
}

TEST_F(BlockchainFrontendTest, BasicInitialState) {
	BlockchainBackend backend(logger, testDir, makeDesign());
	BlockchainFrontend frontend(backend);

	EXPECT_EQ(frontend.getBlockHeight(), 0);
	EXPECT_EQ(frontend.getMempoolSize(), 0);
	EXPECT_EQ(currentSum(frontend), 0);
}

TEST_F(BlockchainFrontendTest, AddTransactionToMempool) {
	BlockchainBackend backend(logger, testDir, makeDesign());
	BlockchainFrontend frontend(backend);

	sp<TestTransaction> tx = sp<TestTransaction>::create(10);
	frontend.sendTransaction(tx);

	EXPECT_EQ(frontend.getMempoolSize(), 1);
	EXPECT_EQ(currentSum(frontend), 10);
}

TEST_F(BlockchainFrontendTest, BuildBlockOnTransactionCount) {
	BlockchainConfig config;
	config.targetBlockTimeMs = 1000;
	config.targetThroughput = 10;

	BlockchainBackend backend(logger, testDir, makeDesign(), config);
	BlockchainFrontend frontend(backend, config);

	for (int i = 0; i < 30; ++i) {
		frontend.sendTransaction(sp<TestTransaction>::create(1));
	}

	// It should build a block quickly because we have 30 transactions and target is 10
	// We wait a bit for the background thread
	int retries = 100;
	while (frontend.getBlockHeight() == 0 && retries-- > 0) {
		usleep(10000);
	}

	EXPECT_GT(frontend.getBlockHeight(), 0);
	EXPECT_LT(frontend.getMempoolSize(), 30);
	EXPECT_EQ(currentSum(frontend), 30);
}

TEST_F(BlockchainFrontendTest, BasicStateRecovery) {
	{
		BlockchainConfig config;
		config.targetBlockTimeMs = 100;
		config.targetThroughput = 10;
		BlockchainBackend backend(logger, testDir, makeDesign(), config);
		BlockchainFrontend frontend(backend, config);

		for (int i = 0; i < 30; ++i) {
			frontend.sendTransaction(sp<TestTransaction>::create(2));
		}

		// Wait for block to be built
		int retries = 50;
		while (frontend.getBlockHeight() == 0 && retries-- > 0) {
			usleep(20000);
		}
		EXPECT_GT(frontend.getBlockHeight(), 0);
		EXPECT_EQ(currentSum(frontend), 60);
	}

	// Reload
	{
		BlockchainBackend backend(logger, testDir, makeDesign());
		BlockchainFrontend frontend(backend);
		EXPECT_GT(frontend.getBlockHeight(), 0);
		EXPECT_EQ(currentSum(frontend), 60);
	}
}

TEST_F(BlockchainFrontendTest, AdvancedStateRecovery) {
	{
		BlockchainConfig config;
		config.targetBlockTimeMs = 100;
		config.targetThroughput = 10;
		BlockchainBackend backend(logger, testDir, makeDesign(), config);
		BlockchainFrontend frontend(backend, config);

		for (int i = 0; i < 30; ++i) {
			frontend.sendTransaction(sp<TestTransaction>::create(1));
		}

		// Wait for at least one block
		int retries = 50;
		while (frontend.getBlockHeight() == 0 && retries-- > 0) {
			usleep(20000);
		}
		EXPECT_GT(frontend.getBlockHeight(), 0);

		// Add more to mempool
		frontend.sendTransaction(sp<TestTransaction>::create(10));
		frontend.sendTransaction(sp<TestTransaction>::create(20));
		EXPECT_GE(frontend.getMempoolSize(), 2);
		EXPECT_EQ(currentSum(frontend), 30 + 10 + 20);
	}

	// Reload
	{
		BlockchainBackend backend(logger, testDir, makeDesign());
		BlockchainFrontend frontend(backend);
		// Wait a bit for mempool to load and background thread to possibly do something
		usleep(200000);

		// It should NOT have built a new block yet from those 2 transactions
		EXPECT_EQ(frontend.getBlockHeight(), 1);
		// Mempool should have been recovered
		EXPECT_GE(frontend.getMempoolSize(), 2);
		EXPECT_EQ(currentSum(frontend), 60);
	}
}

TEST_F(BlockchainFrontendTest, TimeQueryFeature) {
	BlockchainBackend backend(logger, testDir, makeDesign());
	BlockchainFrontend frontend(backend);

	uint64_t start = millis_since_epoch();
	usleep(10000);
	sp<TestTransaction> tx = sp<TestTransaction>::create(500);
	frontend.sendTransaction(tx);
	usleep(10000);
	uint64_t end = millis_since_epoch();

	// Force build block to index it?
	// Or does it query mempool too? "Time query feature: get transaction(s) in a given window of time"
	// Usually it should query both.

	ArrayList<sp<Transaction>> results = frontend.getTransactionsByTimeWindow(start, end);
	EXPECT_GE(results.size(), 1);
}

TEST_F(BlockchainFrontendTest, TransactionQueryById) {
	BlockchainConfig config;
	config.targetBlockTimeMs = 10;
	config.targetThroughput = 10;
	BlockchainBackend backend(logger, testDir, makeDesign(), config);
	BlockchainFrontend frontend(backend, config);

	// Add transactions and wait for block
	for (int i = 0; i < 20; ++i) {
		frontend.sendTransaction(sp<TestTransaction>::create(i));
	}

	int retries = 50;
	while (frontend.getBlockHeight() == 0 && retries-- > 0) {
		usleep(5000);
	}

	// ID = (blockHeight << 20) | index
	uint64_t id = (0ULL << 20) | 3;
	sp<Transaction> tx4 = frontend.getTransactionById(id);
	ASSERT_TRUE((bool)tx4);

	// MEVBuilder sorts by value descending.  Top 10 are 19, 18, 17, 16, 15, 14, 13, 12, 11, 10.
	// Index 3 is value 16.
	EXPECT_EQ(((TestTransaction&)*tx4).value, 16);
}

TEST_F(BlockchainFrontendTest, BlockBuilderTiming) {
	BlockchainConfig config;
	config.targetBlockTimeMs = 1000;
	config.targetThroughput = 10;
	BlockchainBackend backend(logger, testDir, makeDesign(), config);
	BlockchainFrontend frontend(backend, config);

	// Sanity check
	EXPECT_EQ(frontend.getBlockHeight(), 0);

	// First create the initial block.  This block has different timing logic from the rest,
	// because there is not yet any block to establish a block time.
	// This logic also applies when a block has not been built in a long time.
	for (int i = 0; i < 5; ++i) {
		frontend.sendTransaction(sp<TestTransaction>::create(i));
	}

	// Adding half the transaction size target should immediately trigger block creation
	int firstBlockRetries = 100;
	while (frontend.getBlockHeight() == 0 && firstBlockRetries-- > 0) {
		usleep(10000);
	}
	EXPECT_EQ(frontend.getBlockHeight(), 1);

	uint64_t start = millis_since_epoch();
	for (int i = 0; i < 10; ++i) {
		frontend.sendTransaction(sp<TestTransaction>::create(i));
	}

	int retries = 200;
	while (frontend.getBlockHeight() == 1 && retries-- > 0) {
		usleep(10000);
	}
	uint64_t end = millis_since_epoch();
	usleep(20000);
	uint64_t elapsed = end - start;

	EXPECT_NEAR(elapsed, config.targetBlockTimeMs, config.targetBlockTimeMs / 20);
	EXPECT_EQ(frontend.getBlockHeight(), 2);
	EXPECT_NEAR(elapsed, backend.getLastBlockTime(), 10);
}
