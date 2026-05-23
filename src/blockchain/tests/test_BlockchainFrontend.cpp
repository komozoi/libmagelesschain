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

#include "blockchain/BlockchainBackend.h"

#include <filesystem>
#include <unistd.h>

#include "universaltime.h"

#include "blockchain/BlockchainFrontend.h"
#include "blockchain/Transaction.h"
#include "blockchain/BlockchainConfig.h"
#include "blockchain/BlockchainStateSnapshot.h"


namespace fs = std::filesystem;


class TestState : public BlockchainStateSnapshot {
public:
	int sum = 0;
	TestState(BlockchainBackend& backend, long blockNumber) : BlockchainStateSnapshot(backend, blockNumber) {}
};

class TestTransaction : public Transaction {
public:
	int value;
	uint64_t timestamp;

	TestTransaction(int val = 1) : value(val), timestamp(millis_since_epoch()) {}

	bool verify(BlockchainStateSnapshot&) const override { return true; }
	bool write(BlockchainStateSnapshot& snapshot) const override {
		TestState& s = (TestState&)snapshot;
		s.sum += value;
		return true;
	}
	float computeValue(BlockchainStateSnapshot&) const override { return 1.0f; }

	uint8_t getTypeId() const override { return 1; }
	uint64_t getTimestamp() const override { return timestamp; }

	void write(MmapHandle* dst) const override {
		dst->write(getTypeId());
		dst->write(timestamp);
		dst->write(value);
	}

	size_t size() const override { return sizeof(uint8_t) + sizeof(uint64_t) + sizeof(int); }

	static sp<Transaction> createFromMmap(MmapHandle* src) {
		uint64_t ts;
		int val;
		src->read(ts);
		src->read(val);
		sp<TestTransaction> tx = sp<TestTransaction>::create(val);
		tx.mut().timestamp = ts;
		return tx;
	}
};

class BlockchainFrontendTest : public ::testing::Test {
protected:
	std::string testDir;
	Logger logger;

	BlockchainFrontendTest() : logger("cmake-build-debug/test_logs", 0, 0) {
		Transaction::registerType(1, TestTransaction::createFromMmap);
	}

	void SetUp() override {
		uint64_t seconds = millis_since_epoch() / 1000;
		std::string testName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
		testDir = "cmake-build-debug/test_data/" + std::to_string(seconds) + "-" + testName;
		fs::create_directories(testDir);
		fs::create_directories(testDir + "/epochs");
	}

	void TearDown() override {
		if (!testDir.empty() && fs::exists(testDir)) {
			fs::remove_all(testDir);
		}
	}
};

TEST_F(BlockchainFrontendTest, BasicInitialState) {
	BlockchainBackend backend(logger, testDir);
	BlockchainFrontend frontend(backend, sp<TestState>::create(backend, 0));

	EXPECT_EQ(frontend.getBlockHeight(), 0);
	EXPECT_EQ(frontend.getMempoolSize(), 0);
	EXPECT_EQ(((const TestState&)*frontend.getState()).sum, 0);
}

TEST_F(BlockchainFrontendTest, AddTransactionToMempool) {
	BlockchainBackend backend(logger, testDir);
	BlockchainFrontend frontend(backend, sp<TestState>::create(backend, 0));

	sp<TestTransaction> tx = sp<TestTransaction>::create(10);
	frontend.sendTransaction(tx);

	EXPECT_EQ(frontend.getMempoolSize(), 1);
	EXPECT_EQ(((const TestState&)*frontend.getState()).sum, 10);
}

TEST_F(BlockchainFrontendTest, BuildBlockOnTransactionCount) {
	BlockchainConfig config;
	config.targetBlockTimeMs = 1000;
	config.targetThroughput = 10;

	BlockchainBackend backend(logger, testDir, config);
	BlockchainFrontend frontend(backend, sp<TestState>::create(backend, 0), config);

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
	EXPECT_EQ(((const TestState&)*frontend.getState()).sum, 30);
}

TEST_F(BlockchainFrontendTest, BasicStateRecovery) {
	{
		BlockchainConfig config;
		config.targetBlockTimeMs = 100; // Fast for test
		config.targetThroughput = 10;
		BlockchainBackend backend(logger, testDir, config);
		BlockchainFrontend frontend(backend, sp<TestState>::create(backend, 0), config);

		for (int i = 0; i < 30; ++i) {
			frontend.sendTransaction(sp<TestTransaction>::create(2));
		}

		// Wait for block to be built
		int retries = 50;
		while (frontend.getBlockHeight() == 0 && retries-- > 0) {
			usleep(20000);
		}
		EXPECT_GT(frontend.getBlockHeight(), 0);
		EXPECT_EQ(((const TestState&)*frontend.getState()).sum, 60);
	}

	// Reload
	{
		BlockchainBackend backend(logger, testDir);
		BlockchainFrontend frontend(backend, sp<TestState>::create(backend, 0));
		EXPECT_GT(frontend.getBlockHeight(), 0);
		EXPECT_EQ(((const TestState&)*frontend.getState()).sum, 60);
	}
}

TEST_F(BlockchainFrontendTest, AdvancedStateRecovery) {
	{
		BlockchainConfig config;
		config.targetBlockTimeMs = 100;
		config.targetThroughput = 10;
		BlockchainBackend backend(logger, testDir, config);
		BlockchainFrontend frontend(backend, sp<TestState>::create(backend, 0), config);

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
		EXPECT_EQ(((const TestState&)*frontend.getState()).sum, 30 + 10 + 20);
	}

	// Reload
	{
		BlockchainBackend backend(logger, testDir);
		BlockchainFrontend frontend(backend, sp<TestState>::create(backend, 0));
		// Wait a bit for mempool to load and background thread to possibly do something
		usleep(200000);

		// It should NOT have built a new block yet from those 2 transactions
		EXPECT_EQ(frontend.getBlockHeight(), 1);
		// Mempool should have been recovered
		EXPECT_GE(frontend.getMempoolSize(), 2);
		EXPECT_EQ(((const TestState&)*frontend.getState()).sum, 60);
	}
}

TEST_F(BlockchainFrontendTest, TimeQueryFeature) {
	BlockchainBackend backend(logger, testDir);
	BlockchainFrontend frontend(backend, sp<TestState>::create(backend, 0));

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
	BlockchainBackend backend(logger, testDir, config);
	BlockchainFrontend frontend(backend, sp<TestState>::create(backend, 0), config);

	// Add transactions and wait for block
	for (int i = 0; i < 20; ++i) {
		frontend.sendTransaction(sp<TestTransaction>::create(i));
	}

	int retries = 50;
	while (frontend.getBlockHeight() == 0 && retries-- > 0) {
		usleep(5000);
	}

	// ID = (blockHeight << 20) | index
	uint64_t id = (1ULL << 20) | 3;
	sp<Transaction> tx2 = frontend.getTransactionById(id);
	EXPECT_TRUE(tx2);
}

TEST_F(BlockchainFrontendTest, BlockBuilderTiming) {
	BlockchainConfig config;
	config.targetBlockTimeMs = 1000;
	config.targetThroughput = 10;
	BlockchainBackend backend(logger, testDir, config);
	BlockchainFrontend frontend(backend, sp<TestState>::create(backend, 0), config);

	// Sanity check
	EXPECT_EQ(frontend.getBlockHeight(), 0);

	// First create the initial block.  This block has different timing logic from the rest,
	// because there is not yet any block to establish a block time.
	// This logic also applies when a block has not been built in a long time.
	for (int i = 0; i < 5; ++i) {
		frontend.sendTransaction(sp<TestTransaction>::create(i));
	}

	// Adding half the transaction size target should immediately trigger block creation
	usleep(5000);
	EXPECT_EQ(frontend.getBlockHeight(), 1);

    // Create the second block; this should take about 1s (within 5% = 50ms)
    uint64_t start = millis_since_epoch();
    for (int i = 0; i < 10; ++i) {
        frontend.sendTransaction(sp<TestTransaction>::create(i));
    }

    // Wait for block
    int retries = 200;
    while (frontend.getBlockHeight() == 1 && retries-- > 0) {
        usleep(10000);
    }
    uint64_t end = millis_since_epoch();
    uint64_t elapsed = end - start;

    // Should take about 1s (within 5% = 50ms)
    EXPECT_NEAR(elapsed, config.targetBlockTimeMs, config.targetBlockTimeMs / 20);
    EXPECT_EQ(frontend.getBlockHeight(), 2);

    // Check that the block time measurement is correct
    EXPECT_NEAR(elapsed, backend.getLastBlockTime(), 10);
}
