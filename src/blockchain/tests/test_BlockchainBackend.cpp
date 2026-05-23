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
#include <unistd.h>
#include "blockchain/BlockchainBackend.h"
#include "universaltime.h"
#include "blockchain/Transaction.h"
#include "blockchain/BlockchainConfig.h"

#include "testutil/TestChainDesign.h"

namespace fs = std::filesystem;



class BlockchainBackendTest : public ::testing::Test {
protected:
	std::string testDir;
	Logger logger;

	BlockchainBackendTest() : logger("cmake-build-debug/test_logs", 0, 0) {
		Transaction::registerType(1, TestTransaction::createFromMmap);
	}

	void SetUp() override {
		uint64_t seconds = millis_since_epoch() / 1000;
		std::string testName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
		testDir = "cmake-build-debug/test_data/" + std::to_string(seconds) + "-" + testName;
		fs::create_directories(testDir);
	}

	void TearDown() override {
		if (!testDir.empty() && fs::exists(testDir)) {
			fs::remove_all(testDir);
		}
	}
};

TEST_F(BlockchainBackendTest, InitialState) {
	BlockchainBackend backend(logger, testDir);
	EXPECT_EQ(backend.getBlockHeight(), 0);
	EXPECT_EQ(backend.getLastBlockTimestamp(), 0);
}

TEST_F(BlockchainBackendTest, AddAndGetBlock) {
	BlockchainConfig config;
	config.targetBlockTimeMs = 100;
	config.targetThroughput = 1;
	BlockchainBackend backend(logger, testDir, config);

	ArrayList<sp<Transaction>> txs;
	txs.add(sp<TestTransaction>::create(100));
	txs.add(sp<TestTransaction>::create(200));

	long height = backend.addBlock(txs);
	EXPECT_EQ(height, 0);
	EXPECT_EQ(backend.getBlockHeight(), 1);

	ArrayList<sp<Transaction>> retrievedTxs = backend.getBlock(0);
	EXPECT_EQ(retrievedTxs.size(), 2);
	EXPECT_EQ(((const TestTransaction&)*retrievedTxs.get(0)).value, 100);
	EXPECT_EQ(((const TestTransaction&)*retrievedTxs.get(1)).value, 200);
}

TEST_F(BlockchainBackendTest, MiningTooEarly) {
	BlockchainConfig config;
	config.targetBlockTimeMs = 1000;
	config.targetThroughput = 10;
	BlockchainBackend backend(logger, testDir, config);

	ArrayList<sp<Transaction>> txs;
	txs.add(sp<TestTransaction>::create(1));

	// First block
	backend.addBlock(txs);
	EXPECT_EQ(backend.getBlockHeight(), 1);

	// Second block immediately - should fail
	long height = backend.addBlock(txs);
	EXPECT_EQ(height, -1);
	EXPECT_EQ(backend.getBlockHeight(), 1);
}

TEST_F(BlockchainBackendTest, Persistence) {
	{
		BlockchainConfig config;
		config.targetBlockTimeMs = 100;
		BlockchainBackend backend(logger, testDir, config);
		ArrayList<sp<Transaction>> txs;
		txs.add(sp<TestTransaction>::create(42));
		backend.addBlock(txs);
		EXPECT_EQ(backend.getBlockHeight(), 1);
	}

	{
		BlockchainBackend backend(logger, testDir);
		EXPECT_EQ(backend.getBlockHeight(), 1);
		ArrayList<sp<Transaction>> retrievedTxs = backend.getBlock(0);
		EXPECT_EQ(retrievedTxs.size(), 1);
		EXPECT_EQ(((const TestTransaction&)*retrievedTxs.get(0)).value, 42);
	}
}

TEST_F(BlockchainBackendTest, InvalidBlockRange) {
	BlockchainBackend backend(logger, testDir);
	EXPECT_THROW(backend.getBlock(0), std::range_error);
	EXPECT_THROW(backend.getBlock(1), std::range_error);
}

TEST_F(BlockchainBackendTest, TimeWindowQuery) {
	BlockchainConfig config;
	config.targetBlockTimeMs = 100;
	config.targetThroughput = 1;
	BlockchainBackend backend(logger, testDir, config);

	uint64_t t1 = millis_since_epoch();
	usleep(1000);
	
	ArrayList<sp<Transaction>> txs1;
	txs1.add(sp<TestTransaction>::create(1));
	EXPECT_EQ(backend.addBlock(txs1), 0);
	
	// Wait to ensure second block has distinct timestamp and satisfies timing
	usleep(110000); 
	uint64_t t2 = millis_since_epoch();
	usleep(1000);

	ArrayList<sp<Transaction>> txs2;
	txs2.add(sp<TestTransaction>::create(2));
	EXPECT_EQ(backend.addBlock(txs2), 1);
	
	usleep(1000);
	uint64_t t3 = millis_since_epoch();

	ArrayList<sp<Transaction>> results12 = backend.getTransactionsByTimeWindow(t1, t2);
	EXPECT_EQ(results12.size(), 1);
	EXPECT_EQ(((const TestTransaction&)*results12.get(0)).value, 1);

	ArrayList<sp<Transaction>> results23 = backend.getTransactionsByTimeWindow(t2, t3);
	EXPECT_EQ(results23.size(), 1);
	EXPECT_EQ(((const TestTransaction&)*results23.get(0)).value, 2);

	ArrayList<sp<Transaction>> results13 = backend.getTransactionsByTimeWindow(t1, t3);
	EXPECT_EQ(results13.size(), 2);
}

TEST_F(BlockchainBackendTest, MultipleBlocksInOneEpoch) {
	BlockchainConfig config;
	config.targetBlockTimeMs = 10;
	config.targetThroughput = 1;
	BlockchainBackend backend(logger, testDir, config);

	for (int i = 1; i <= 10; ++i) {
		ArrayList<sp<Transaction>> txs;
		txs.add(sp<TestTransaction>::create(i));
		while(backend.addBlock(txs) == -1) {
			usleep(10000);
		}
	}

	EXPECT_EQ(backend.getBlockHeight(), 10);
	for (int i = 0; i < 10; ++i) {
		ArrayList<sp<Transaction>> retrievedTxs = backend.getBlock(i);
		EXPECT_EQ(retrievedTxs.size(), 1);
		EXPECT_EQ(((const TestTransaction&)*retrievedTxs.get(0)).value, i + 1);
	}
}
