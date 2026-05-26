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
#include <Logger.h>

#include "storage/Catalog.h"
#include "storage/CatalogFile.h"
#include "universaltime.h"
#include "ds/Bytestring.h"
#include "parallel/ThreadPool.h"


/*
 * Direct unit tests for the Catalog: write/scan, cross-index isolation,
 * range filtering, and durability across reopens.
 */
class CatalogTest : public ::testing::Test {
protected:
	std::string testDir;
	ThreadPool* executor = nullptr;

	void SetUp() override {
		uint64_t seconds = millis_since_epoch() / 1000;
		std::string testName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
		testDir = "test_data/" + std::to_string(seconds) + "-Catalog-" + testName;
		std::filesystem::create_directories(testDir);
		executor = new ThreadPool(8);
	}

	void TearDown() override {
		delete executor;
		executor = nullptr;
		if (!HasFailure() && !testDir.empty() && std::filesystem::exists(testDir))
			std::filesystem::remove_all(testDir);
	}

	static Bytestring makePayload(uint8_t marker, size_t size = 8) {
		uint8_t* tmp = new uint8_t[size];
		for (size_t i = 0; i < size; ++i) tmp[i] = (uint8_t)(marker + i);
		Bytestring buf((void*)tmp, size);
		delete[] tmp;
		return buf;
	}

	static Bytestring makeLargePayload(uint8_t marker, size_t size = 368 * 1024 * 1024) {
		uint8_t* tmp = new uint8_t[size];
		uint64_t mixer = 0x783a1d9e27d46172 * (marker + 97);
		for (size_t i = 0; i < size; ++i) {
			tmp[i] = (uint8_t)(mixer >> (mixer % 56));
			mixer *= 0xd3017b2483f3979a;
		}

		Bytestring buf(tmp, size);
		delete[] tmp;
		return buf;
	}

	/*
	 * Block until pending background writes finish, then count live
	 * segments for `indexId` in `[start,end]` by walking every catalog
	 * file in range.
	 */
	static int countSegments(Catalog& cat, uint16_t indexId, uint64_t start = 0, uint64_t end = UINT64_MAX) {
		ArrayList<uint64_t> fileIds = cat.rangeScan(indexId, start, end);
		int total = 0;
		for (int i = 0; i < fileIds.size(); ++i) {
			sp<CatalogFile> file = cat.getCatalogFile(fileIds.get(i));
			if (!file) continue;
			file.mut().openForReading<int>([&](CatalogFileReader& reader) {
				reader.forEachSegment(indexId, start, end,
					[&](const segment_btree_metadata_t&) { ++total; });
				return 0;
			});
		}
		return total;
	}

	void waitForWrites() {
		// Background writes go through the catalog's executor; we
		// don't own it.  Spin briefly on a count instead of trying to
		// flush a foreign pool.
	}
};


TEST_F(CatalogTest, EmptyCatalogReturnsNoFiles) {
	Catalog cat(testDir, *executor);
	ArrayList<uint64_t> files = cat.rangeScan(0, 0, UINT64_MAX);
	EXPECT_EQ(files.size(), 0);
}


TEST_F(CatalogTest, WrittenSegmentIsRetrievable) {
	Catalog cat(testDir, *executor);
	cat.writeSegment(0, 1, 0, 0, 1, makePayload(7));

	// Spin until the background write reaches the catalog.
	int seen = 0;
	for (int spin = 0; spin < 200 && seen == 0; ++spin) {
		seen = countSegments(cat, 0);
		if (seen == 0)
			usleep(5000);
	}
	EXPECT_EQ(seen, 1);
}


TEST_F(CatalogTest, MultipleSegmentsRetrievable) {
	Catalog cat(testDir, *executor);
	for (uint64_t b = 0; b < 5; ++b)
		cat.writeSegment(0, 1, 0, b, b + 1, makePayload((uint8_t)b));

	int seen = 0;
	for (int spin = 0; spin < 200 && seen < 5; ++spin) {
		seen = countSegments(cat, 0);
		if (seen < 5) usleep(5000);
	}
	EXPECT_EQ(seen, 5);
}


/*TEST_F(CatalogTest, MultipleSegmentsRetrievableLargePayloads) {
	Catalog cat(testDir, *executor);

	ArrayList<Bytestring> payloads;
	for (uint64_t b = 0; b < 5; ++b)
		payloads.add(makeLargePayload((uint8_t)b));
	for (uint64_t b = 0; b < 5; ++b)
		cat.writeSegment(0, 1, 0, b, b + 1, std::move(payloads.get(b)));

	int seen = 0;
	for (int spin = 0; spin < 200 && seen < 5; ++spin) {
		seen = countSegments(cat, 0);
		if (seen < 5) usleep(5000);
	}
	EXPECT_EQ(seen, 5);
}


TEST_F(CatalogTest, MultipleSegmentsRetrievableHugePayloads) {
	Logger logger("test_logs1", LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG);
	LogEndpoint log(logger, "test");
	Catalog cat(testDir, *executor);
	ArrayList<Bytestring> payloads;
	for (uint64_t b = 0; b < 5; ++b)
		payloads.add(makeLargePayload((uint8_t)b, 1024 * 1024 * 1024));
	for (uint64_t b = 0; b < 5; ++b) {
		log.info("Writing segment...");
		cat.writeSegment(0, 1, 0, b, b + 1, std::move(payloads.get(b)));
		log.info("Done writing segment.");
	}

	int seen = 0;
	for (int spin = 0; spin < 200 && seen < 5; ++spin) {
		seen = countSegments(cat, 0);
		if (seen < 5) usleep(5000);
	}
	EXPECT_EQ(seen, 5);
}*/


TEST_F(CatalogTest, CrossIndexIsolation) {
	Catalog cat(testDir, *executor);
	cat.writeSegment(0, 1, 0, 0, 1, makePayload(1));
	cat.writeSegment(1, 1, 0, 0, 1, makePayload(2));
	cat.writeSegment(2, 1, 0, 0, 1, makePayload(3));

	int aSeen = 0, bSeen = 0, cSeen = 0, dSeen = 0;
	for (int spin = 0; spin < 200; ++spin) {
		aSeen = countSegments(cat, 0);
		bSeen = countSegments(cat, 1);
		cSeen = countSegments(cat, 2);
		dSeen = countSegments(cat, 99);
		if (aSeen == 1 && bSeen == 1 && cSeen == 1) break;
		usleep(5000);
	}
	EXPECT_EQ(aSeen, 1);
	EXPECT_EQ(bSeen, 1);
	EXPECT_EQ(cSeen, 1);
	EXPECT_EQ(dSeen, 0);
}


TEST_F(CatalogTest, RangeScanFiltersByBlockRange) {
	Catalog cat(testDir, *executor);
	for (uint64_t b = 0; b < 10; ++b)
		cat.writeSegment(0, 1, 0, b, b + 1, makePayload((uint8_t)b));

	int total = 0;
	for (int spin = 0; spin < 400 && total < 10; ++spin) {
		total = countSegments(cat, 0);
		if (total < 10) usleep(5000);
	}
	ASSERT_EQ(total, 10);

	int windowed = countSegments(cat, 0, 3, 5);
	EXPECT_EQ(windowed, 3);
}


TEST_F(CatalogTest, SegmentsSurviveReopen) {
	{
		Catalog cat(testDir, *executor);
		cat.writeSegment(0, 1, 0, 0, 1, makePayload(1));
		cat.writeSegment(0, 1, 0, 1, 2, makePayload(2));

		int total = 0;
		for (int spin = 0; spin < 400 && total < 2; ++spin) {
			total = countSegments(cat, 0);
			if (total < 2) usleep(5000);
		}
		ASSERT_EQ(total, 2);
	}
	{
		Catalog cat(testDir, *executor);
		EXPECT_EQ(countSegments(cat, 0), 2);
	}
}


TEST_F(CatalogTest, PayloadRoundTripsThroughReader) {
	Catalog cat(testDir, *executor);
	Bytestring expected = makePayload(0x42, 16);
	cat.writeSegment(7, 1, 0, 100, 101, Bytestring(expected));

	uint8_t observed[16] = {};
	bool found = false;
	for (int spin = 0; spin < 400 && !found; ++spin) {
		ArrayList<uint64_t> files = cat.rangeScan(7, 0, UINT64_MAX);
		for (int i = 0; i < files.size() && !found; ++i) {
			sp<CatalogFile> file = cat.getCatalogFile(files.get(i));
			if (!file) continue;
			file.mut().openForReading<int>([&](CatalogFileReader& reader) {
				reader.forEachSegment(7, 0, UINT64_MAX,
					[&](const segment_btree_metadata_t& s) {
						MmapHandle view = reader.openEntry(s.byteOffset, s.byteLength);
						const uint8_t* data = view.directPointer<uint8_t>();
						if (data && s.byteLength == 16) {
							memcpy(observed, data, 16);
							found = true;
						}
					});
				return 0;
			});
		}
		if (!found) usleep(5000);
	}
	ASSERT_TRUE(found);
	for (int i = 0; i < 16; ++i)
		EXPECT_EQ(observed[i], expected[i]);
}
