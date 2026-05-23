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
#include "blockchain/MEVBuilder.h"
#include "blockchain/BlockchainBackend.h"
#include "testutil/TestChainDesign.h"
#include "universaltime.h"
#include <filesystem>

class MEVBuilderTest : public ::testing::Test {
protected:
    std::string testDir;
    Logger logger;

    MEVBuilderTest() : logger("cmake-build-debug/test_logs", 0, 0) {}

    void SetUp() override {
        uint64_t seconds = millis_since_epoch() / 1000;
        testDir = "cmake-build-debug/test_data/" + std::to_string(seconds) + "-MEVBuilderTest";
        std::filesystem::create_directories(testDir);
    }

    void TearDown() override {
        if (std::filesystem::exists(testDir)) {
            std::filesystem::remove_all(testDir);
        }
    }
};

TEST_F(MEVBuilderTest, BasicSelection) {
    BlockchainBackend backend(logger, testDir, sp<TestState>::create(backend, 0));
    MEVBuilder builder(backend);
    sp<BlockchainStateSnapshot> initialState = backend.getLatestState();

    ArrayList<sp<Transaction>> mempool;
    mempool.add(sp<TestTransaction>::create(1));
    mempool.add(sp<TestTransaction>::create(5));
    mempool.add(sp<TestTransaction>::create(3));

    ArrayList<sp<Transaction>> block = builder.buildBlock(mempool, 2, 0);

    EXPECT_EQ(block.size(), 2);
    EXPECT_EQ(block.get(0)->computeValue(initialState.mut()), 5.0f);
    EXPECT_EQ(block.get(1)->computeValue(initialState.mut()), 3.0f);
    EXPECT_EQ(mempool.size(), 1);
    EXPECT_EQ(mempool.get(0)->computeValue(initialState.mut()), 1.0f);
}

TEST_F(MEVBuilderTest, RespectMaxTransactions) {
    BlockchainBackend backend(logger, testDir, sp<TestState>::create(backend, 0));
    MEVBuilder builder(backend);
    sp<BlockchainStateSnapshot> initialState = backend.getLatestState();

    ArrayList<sp<Transaction>> mempool;
    for (int i = 0; i < 10; ++i) {
        mempool.add(sp<TestTransaction>::create(i));
    }

    ArrayList<sp<Transaction>> block = builder.buildBlock(mempool, 5, 0);

    EXPECT_EQ(block.size(), 5);
    EXPECT_EQ(mempool.size(), 5);
    EXPECT_EQ(block.get(0)->computeValue(initialState.mut()), 9.0f);
    EXPECT_EQ(block.get(4)->computeValue(initialState.mut()), 5.0f);
}

TEST_F(MEVBuilderTest, EmptyMempool) {
    BlockchainBackend backend(logger, testDir, sp<TestState>::create(backend, 0));
    MEVBuilder builder(backend);

    ArrayList<sp<Transaction>> mempool;
    ArrayList<sp<Transaction>> block = builder.buildBlock(mempool, 10, 0);

    EXPECT_EQ(block.size(), 0);
    EXPECT_EQ(mempool.size(), 0);
}

TEST_F(MEVBuilderTest, ComplexExecutionOrderDependency) {
    BlockchainBackend backend(logger, testDir, sp<TestState>::create(backend, 0));
    MEVBuilder builder(backend);

    ArrayList<sp<Transaction>> mempool;
    // Tx A: id=1, value 10
    // Tx B: id=2, value 5 initially, 20 if count > 0
    // Tx C: id=3, value 15 initially, 1 if count > 0
    
    mempool.add(sp<TestTransaction>::create(0, 1));
    mempool.add(sp<TestTransaction>::create(0, 2));
    mempool.add(sp<TestTransaction>::create(0, 3));

    // Iterative logic with maxTransactions = 2:
    // 1. Initially values: Tx 1=10, Tx 2=5, Tx 3=15. Pick Tx 3 (15).
    // 2. count becomes 1.
    // 3. Re-evaluate remaining: Tx 1=10, Tx 2=20.
    // 4. Pick Tx 2 (20).
    // Result block: [Tx 3, Tx 2]. Total value = 15 + 20 = 35.
    
    // Note: Simple sort on initial values would pick [Tx 3, Tx 1], total = 15 + 10 = 25.
    
    ArrayList<sp<Transaction>> block = builder.buildBlock(mempool, 2, 0);
    EXPECT_EQ(block.size(), 2);
    EXPECT_EQ(((TestTransaction&)*block.get(0)).id, 3);
    EXPECT_EQ(((TestTransaction&)*block.get(1)).id, 2);
    
    // Verify remaining in mempool
    EXPECT_EQ(mempool.size(), 1);
    EXPECT_EQ(((TestTransaction&)*mempool.get(0)).id, 1);
}

TEST_F(MEVBuilderTest, ExecutionOrderDependency) {
    BlockchainBackend backend(logger, testDir, sp<TestState>::create(backend, 0));
    MEVBuilder builder(backend);

    ArrayList<sp<Transaction>> mempool;
    mempool.add(sp<TestTransaction>::create(0, 2)); // Value 5 initially (id=2)
    mempool.add(sp<TestTransaction>::create(0, 1)); // Value 10 initially (id=1)

    // Iterative logic should:
    // 1. Evaluate Tx 2 (val 5), Tx 1 (val 10). Pick Tx 1.
    // 2. Apply Tx 1. count becomes 1.
    // 3. Evaluate remaining Tx 2. Now val is 20!
    // 4. Pick Tx 2.
    // Result block: [Tx 1, Tx 2].
    
    ArrayList<sp<Transaction>> block = builder.buildBlock(mempool, 10, 0);
    EXPECT_EQ(block.size(), 2);
    EXPECT_EQ(((TestTransaction&)*block.get(0)).id, 1);
    EXPECT_EQ(((TestTransaction&)*block.get(1)).id, 2);
}
