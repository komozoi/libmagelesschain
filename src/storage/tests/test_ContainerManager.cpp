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
#include <cstring>

#include "storage/IndexContainerManager.h"
#include "universaltime.h"

/*
 * Direct unit tests for IndexContainer + IndexContainerManager: payload
 * round-trip, free-region reuse, container caching, and persistence of
 * written data across manager re-opens.
 */

class ContainerManagerTest : public ::testing::Test {
protected:
	std::string testDir;

	void SetUp() override {
		uint64_t seconds = millis_since_epoch() / 1000;
		std::string testName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
		testDir = "cmake-build-debug/test_data/" + std::to_string(seconds) + "-CM-" + testName;
		std::filesystem::create_directories(testDir);
	}

	void TearDown() override {
		if (!testDir.empty() && std::filesystem::exists(testDir))
			std::filesystem::remove_all(testDir);
	}

	static Bytestring makePayload(const char* data) {
		return Bytestring((void*)data, strlen(data));
	}
};

TEST_F(ContainerManagerTest, WriteReadRoundTrip) {
	IndexContainerManager mgr(testDir);
	IndexContainer* c = mgr.get(0, 0, 0);
	ASSERT_NE(c, nullptr);

	Bytestring in = makePayload("hello, segments");
	uint64_t offset = c->writePayload(in);
	IndexContainer::PayloadView view = c->mmapPayload(offset, in.size());

	ASSERT_EQ(view.length, in.size());
	EXPECT_EQ(memcmp(&in[0], view.data, in.size()), 0);
}

TEST_F(ContainerManagerTest, CachingReturnsSameInstance) {
	IndexContainerManager mgr(testDir);
	IndexContainer* a = mgr.get(0, 0, 0);
	IndexContainer* b = mgr.get(0, 0, 0);
	EXPECT_EQ(a, b);
}

TEST_F(ContainerManagerTest, DistinctCoordsDistinctContainers) {
	IndexContainerManager mgr(testDir);
	IndexContainer* a = mgr.get(0, 0, 0);
	IndexContainer* b = mgr.get(0, 1, 0);
	IndexContainer* c = mgr.get(1, 0, 0);
	EXPECT_NE(a, b);
	EXPECT_NE(a, c);
	EXPECT_NE(b, c);
}

TEST_F(ContainerManagerTest, PayloadPersistsAcrossReopen) {
	Bytestring in = makePayload("durable segment payload");
	uint64_t off = 0;
	uint64_t len = in.size();
	{
		IndexContainerManager mgr(testDir);
		IndexContainer* c = mgr.get(0, 0, 0);
		off = c->writePayload(in);
	}
	{
		IndexContainerManager mgr(testDir);
		IndexContainer* c = mgr.get(0, 0, 0);
		IndexContainer::PayloadView view = c->mmapPayload(off, len);
		ASSERT_EQ(view.length, len);
		EXPECT_EQ(memcmp(&in[0], view.data, len), 0);
	}
}

TEST_F(ContainerManagerTest, FreeRegionDoesNotCorruptOtherPayloads) {
	IndexContainerManager mgr(testDir);
	IndexContainer* c = mgr.get(0, 0, 0);
	Bytestring a = makePayload("AAAA");
	Bytestring b = makePayload("BBBB");
	uint64_t offA = c->writePayload(a);
	uint64_t offB = c->writePayload(b);

	c->freeRegion(offA, a.size());
	// B must still be intact after A's region is released.
	IndexContainer::PayloadView view = c->mmapPayload(offB, b.size());
	ASSERT_EQ(view.length, b.size());
	EXPECT_EQ(memcmp(&b[0], view.data, b.size()), 0);
}

TEST_F(ContainerManagerTest, RefusesEmptyPayload) {
	IndexContainerManager mgr(testDir);
	IndexContainer* c = mgr.get(0, 0, 0);
	Bytestring empty;
	EXPECT_THROW(c->writePayload(empty), std::invalid_argument);
}
