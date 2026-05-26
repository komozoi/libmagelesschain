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

#include "storage/Catalog.h"
#include "universaltime.h"

/*
 * Direct unit tests for the Catalog: insert/scan, removal,
 * cross-instance isolation, and durability of removals across
 * Catalog re-opens.
 */

class CatalogTest : public ::testing::Test {
protected:
	std::string testDir;

	void SetUp() override {
		uint64_t seconds = millis_since_epoch() / 1000;
		std::string testName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
		testDir = "cmake-build-debug/test_data/" + std::to_string(seconds) + "-Catalog-" + testName;
		std::filesystem::create_directories(testDir);
	}

	void TearDown() override {
		if (!testDir.empty() && std::filesystem::exists(testDir))
			std::filesystem::remove_all(testDir);
	}

	static segment_btree_metadata_t makeLocator(uint16_t typeId, uint8_t instId, uint64_t start, uint64_t end,
		uint32_t mergeGen = 0, uint64_t byteLength = 8) {
		segment_btree_metadata_t loc = {};
		loc.persistentTypeId = typeId;
		loc.instanceId = instId;
		loc.encodingVersion = 1;
		loc.mergeGeneration = mergeGen;
		loc.blockRangeStart = start;
		loc.blockRangeEnd = end;
		loc.containerId = 0;
		loc.byteOffset = 0;
		loc.byteLength = byteLength;
		loc.checksum = 0;
		return loc;
	}
};

TEST_F(CatalogTest, EmptyCatalogReturnsNoSegments) {
	Catalog cat(testDir);
	EXPECT_EQ(cat.countSegments(0, 0), 0);
	ArrayList<segment_btree_metadata_t> segs = cat.getAllSegments(0, 0);
	EXPECT_EQ(segs.size(), 0);
}

TEST_F(CatalogTest, InsertedSegmentIsRetrievable) {
	Catalog cat(testDir);
	segment_btree_metadata_t loc = makeLocator(0, 0, 0, 0);
	cat.insert(loc);
	EXPECT_EQ(cat.countSegments(0, 0), 1);
	ArrayList<segment_btree_metadata_t> segs = cat.getAllSegments(0, 0);
	ASSERT_EQ(segs.size(), 1);
	EXPECT_EQ(segs.get(0).blockRangeStart, 0u);
}

TEST_F(CatalogTest, MultipleSegmentsAscendingBlockOrder) {
	Catalog cat(testDir);
	cat.insert(makeLocator(0, 0, 5, 5));
	cat.insert(makeLocator(0, 0, 0, 0));
	cat.insert(makeLocator(0, 0, 3, 3));

	ArrayList<segment_btree_metadata_t> segs = cat.getAllSegments(0, 0);
	ASSERT_EQ(segs.size(), 3);
	EXPECT_EQ(segs.get(0).blockRangeStart, 0u);
	EXPECT_EQ(segs.get(1).blockRangeStart, 3u);
	EXPECT_EQ(segs.get(2).blockRangeStart, 5u);
}

TEST_F(CatalogTest, CrossInstanceIsolation) {
	Catalog cat(testDir);
	cat.insert(makeLocator(0, 0, 0, 0));
	cat.insert(makeLocator(0, 1, 0, 0));
	cat.insert(makeLocator(1, 0, 0, 0));

	EXPECT_EQ(cat.countSegments(0, 0), 1);
	EXPECT_EQ(cat.countSegments(0, 1), 1);
	EXPECT_EQ(cat.countSegments(1, 0), 1);
	EXPECT_EQ(cat.countSegments(2, 0), 0);
}

TEST_F(CatalogTest, RangeScanFiltersByBlockRange) {
	Catalog cat(testDir);
	for (uint64_t b = 0; b < 10; ++b) cat.insert(makeLocator(0, 0, b, b));

	ArrayList<segment_btree_metadata_t> segs = cat.rangeScan(0, 0, 3, 5);
	ASSERT_EQ(segs.size(), 3);
	EXPECT_EQ(segs.get(0).blockRangeStart, 3u);
	EXPECT_EQ(segs.get(2).blockRangeStart, 5u);
}

TEST_F(CatalogTest, RemoveDropsFromScan) {
	Catalog cat(testDir);
	segment_btree_metadata_t a = makeLocator(0, 0, 0, 0);
	segment_btree_metadata_t b = makeLocator(0, 0, 1, 1);
	cat.insert(a);
	cat.insert(b);
	EXPECT_EQ(cat.countSegments(0, 0), 2);

	cat.remove(a);
	EXPECT_EQ(cat.countSegments(0, 0), 1);

	ArrayList<segment_btree_metadata_t> segs = cat.getAllSegments(0, 0);
	ASSERT_EQ(segs.size(), 1);
	EXPECT_EQ(segs.get(0).blockRangeStart, 1u);
}

TEST_F(CatalogTest, RemovalSurvivesReopen) {
	segment_btree_metadata_t a = makeLocator(0, 0, 0, 0);
	segment_btree_metadata_t b = makeLocator(0, 0, 1, 1);
	{
		Catalog cat(testDir);
		cat.insert(a);
		cat.insert(b);
		cat.remove(a);
	}
	{
		Catalog cat(testDir);
		EXPECT_EQ(cat.countSegments(0, 0), 1);
		ArrayList<segment_btree_metadata_t> segs = cat.getAllSegments(0, 0);
		ASSERT_EQ(segs.size(), 1);
		EXPECT_EQ(segs.get(0).blockRangeStart, 1u);
	}
}

TEST_F(CatalogTest, MergeGenerationsCoexistAtSameBlockStart) {
	Catalog cat(testDir);
	cat.insert(makeLocator(0, 0, 0, 0, /*mergeGen=*/0));
	cat.insert(makeLocator(0, 0, 0, 5, /*mergeGen=*/1));
	EXPECT_EQ(cat.countSegments(0, 0), 2);
}
