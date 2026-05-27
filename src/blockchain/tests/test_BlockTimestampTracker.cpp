/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-27
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
#include "blockchain/BlockTimestampTracker.h"

class BlockTimestampTrackerTest : public ::testing::Test {
protected:
	std::string testDir = "test_timestamp_tracker";

	void SetUp() override {
		std::filesystem::remove_all(testDir);
		std::filesystem::create_directories(testDir);
	}

	void TearDown() override {
		std::filesystem::remove_all(testDir);
	}
};

TEST_F(BlockTimestampTrackerTest, AddAndQuery) {
	BlockTimestampTracker tracker(testDir);

	tracker.addBlock(1000, 0);
	tracker.addBlock(2000, 1);
	tracker.addBlock(3000, 2);
	tracker.addBlock(3000, 3); // Same timestamp
	tracker.addBlock(4000, 4);

	{
		ArrayList<uint64_t> blocks = tracker.getBlocksInWindow(1500, 3500);
		EXPECT_EQ(blocks.size(), 3);
		EXPECT_EQ(blocks.get(0), 1);
		EXPECT_EQ(blocks.get(1), 2);
		EXPECT_EQ(blocks.get(2), 3);
	}

	{
		ArrayList<uint64_t> blocks = tracker.getBlocksInWindow(1000, 1000);
		EXPECT_EQ(blocks.size(), 1);
		EXPECT_EQ(blocks.get(0), 0);
	}

	{
		ArrayList<uint64_t> blocks = tracker.getBlocksInWindow(4000, 5000);
		EXPECT_EQ(blocks.size(), 1);
		EXPECT_EQ(blocks.get(0), 4);
	}

	{
		ArrayList<uint64_t> blocks = tracker.getBlocksInWindow(5000, 6000);
		EXPECT_EQ(blocks.size(), 0);
	}
}

TEST_F(BlockTimestampTrackerTest, Persistent) {
	{
		BlockTimestampTracker tracker(testDir);
		tracker.addBlock(1000, 0);
		tracker.addBlock(2000, 1);
	}

	{
		BlockTimestampTracker tracker(testDir);
		ArrayList<uint64_t> blocks = tracker.getBlocksInWindow(0, 3000);
		EXPECT_EQ(blocks.size(), 2);
		EXPECT_EQ(blocks.get(0), 0);
		EXPECT_EQ(blocks.get(1), 1);
	}
}
