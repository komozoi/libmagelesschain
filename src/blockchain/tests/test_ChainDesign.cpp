/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-24
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

#include "blockchain/BlockchainBackend.h"
#include "blockchain/ChainDesign.h"
#include "testutil/TestChainDesign.h"
#include "universaltime.h"

/*
 * End-to-end ChainDesign plumbing.  Verifies a backend constructed from a
 * minimal ChainDesign correctly drives the three registration hooks,
 * keeps the design alive, and exposes the registered indexes/overrides
 * through the typed accessors.
 */

class ChainDesignTest : public ::testing::Test {
protected:
	std::string testDir;
	Logger logger;

	ChainDesignTest() : logger("cmake-build-debug/test_logs", 0, 0) {}

	void SetUp() override {
		uint64_t seconds = millis_since_epoch() / 1000;
		std::string testName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
		testDir = "test_data/" + std::to_string(seconds) + "-" + testName;
		std::filesystem::create_directories(testDir);
	}

	void TearDown() override {
		if (!testDir.empty() && std::filesystem::exists(testDir))
			std::filesystem::remove_all(testDir);
	}
};

TEST_F(ChainDesignTest, BackendExposesRegisteredIndex) {
	sp<ChainDesign> design = sp<ChainDesign>(sp<TestChainDesign>::create());
	BlockchainBackend backend(logger, testDir, design);
	sp<TestSumIndex> idx = backend.index<TestSumIndex>(0);
	ASSERT_NE(idx.get(), nullptr);
	EXPECT_EQ(idx->latestSum(), 0);
}

TEST_F(ChainDesignTest, BackendStateOverrideHasRegisteredFamily) {
	sp<ChainDesign> design = sp<ChainDesign>(sp<TestChainDesign>::create());
	BlockchainBackend backend(logger, testDir, design);
	sp<StateOverride> state = backend.newStateOverride();
	ASSERT_NE(state.get(), nullptr);
	EXPECT_EQ(state->override<TestSumOverrideFamily>(0).sum(), 0);
	EXPECT_EQ(state->override<TestSumOverrideFamily>(0).count(), 0);
}

TEST_F(ChainDesignTest, TransactionTypeIsRegistered) {
	sp<ChainDesign> design = sp<ChainDesign>(sp<TestChainDesign>::create());
	BlockchainBackend backend(logger, testDir, design);
	const TransactionTypeRegistry& reg = backend.getTransactionTypeRegistry();
	EXPECT_NE(reg.getFactory(1), nullptr);
	EXPECT_EQ(reg.getFactory(0), nullptr);
	EXPECT_EQ(reg.getFactory(2), nullptr);
}

/*
 * A design that registers nothing.  Used to verify a backend can be built
 * with empty registries; queries against unregistered types are tested at
 * the BackendRegistry/StateOverrideRegistry level.
 */
class EmptyChainDesign : public ChainDesign {
public:
	void registerIndexes(BackendRegistry&) override {}
	void registerOverrides(StateOverrideRegistry&) override {}
	void registerTransactionTypes(TransactionTypeRegistry&) override {}
};

TEST_F(ChainDesignTest, EmptyDesignBackendConstructs) {
	sp<ChainDesign> design = sp<ChainDesign>(sp<EmptyChainDesign>::create());
	BlockchainBackend backend(logger, testDir, design);
	EXPECT_EQ(backend.getBlockHeight(), 0);
}
