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
#include "ds/ArrayList.h"
#include "universaltime.h"

/*
 * Direct unit tests for IndexContainerManager: payload round-trip,
 * automatic container selection, free-region reuse, and persistence of
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

	Bytestring in = makePayload("hello, segments");
	IndexContainerManager::WriteResult wr = mgr.write(in);
	EXPECT_EQ(wr.length, in.size());
	EXPECT_GT(wr.containerId, 0u);

	IndexContainer::PayloadView view = mgr.mmapPayload(wr.containerId, wr.offset, wr.length);
	ASSERT_EQ(view.length, in.size());
	EXPECT_EQ(memcmp(&in[0], view.data, in.size()), 0);
}

TEST_F(ContainerManagerTest, ManyPayloadsPackIntoSingleContainer) {
	IndexContainerManager mgr(testDir);
	// 10 small payloads should all land in the first allocated
	// container; the manager only rolls over when the per-container
	// 2 GiB cap is reached.
	ArrayList<IndexContainerManager::WriteResult> results;
	for (int i = 0; i < 10; ++i) {
		Bytestring in = makePayload("payloadXYZ");
		results.add(mgr.write(in));
	}
	uint64_t firstId = results.get(0).containerId;
	for (int i = 1; i < results.size(); ++i) {
		EXPECT_EQ(results.get(i).containerId, firstId);
	}
	EXPECT_EQ(mgr.containerCount(), 1);
}

TEST_F(ContainerManagerTest, PayloadPersistsAcrossReopen) {
	Bytestring in = makePayload("durable segment payload");
	IndexContainerManager::WriteResult wr{};
	{
		IndexContainerManager mgr(testDir);
		wr = mgr.write(in);
	}
	{
		IndexContainerManager mgr(testDir);
		IndexContainer::PayloadView view = mgr.mmapPayload(wr.containerId, wr.offset, wr.length);
		ASSERT_EQ(view.length, in.size());
		EXPECT_EQ(memcmp(&in[0], view.data, in.size()), 0);
		// Existing container is rediscovered on reopen.
		EXPECT_GE(mgr.containerCount(), 1);
	}
}

TEST_F(ContainerManagerTest, FreeRegionDoesNotCorruptOtherPayloads) {
	IndexContainerManager mgr(testDir);
	Bytestring a = makePayload("AAAA");
	Bytestring b = makePayload("BBBB");

	IndexContainerManager::WriteResult wa = mgr.write(a);
	IndexContainerManager::WriteResult wb = mgr.write(b);
	EXPECT_EQ(wa.containerId, wb.containerId); // packed together

	mgr.freeRegion(wa.containerId, wa.offset, wa.length);

	// B must still be intact after A's region is released.
	IndexContainer::PayloadView view = mgr.mmapPayload(wb.containerId, wb.offset, wb.length);
	ASSERT_EQ(view.length, b.size());
	EXPECT_EQ(memcmp(&b[0], view.data, b.size()), 0);
}

TEST_F(ContainerManagerTest, RefusesEmptyPayload) {
	IndexContainerManager mgr(testDir);
	Bytestring empty;
	EXPECT_THROW(mgr.write(empty), std::invalid_argument);
}

TEST_F(ContainerManagerTest, MultiplePayloadsFromDifferentLogicalIndexesShareContainer) {
	// The manager has no notion of which (typeId, instanceId) a payload
	// belongs to; payloads from any source share containers freely.
	// All that matters is that each (containerId, offset, length)
	// uniquely identifies the bytes.
	IndexContainerManager mgr(testDir);
	Bytestring a = makePayload("from index 0");
	Bytestring b = makePayload("from index 1");

	IndexContainerManager::WriteResult wa = mgr.write(a);
	IndexContainerManager::WriteResult wb = mgr.write(b);
	EXPECT_EQ(wa.containerId, wb.containerId);

	IndexContainer::PayloadView va = mgr.mmapPayload(wa.containerId, wa.offset, wa.length);
	IndexContainer::PayloadView vb = mgr.mmapPayload(wb.containerId, wb.offset, wb.length);
	EXPECT_EQ(memcmp(&a[0], va.data, a.size()), 0);
	EXPECT_EQ(memcmp(&b[0], vb.data, b.size()), 0);
}
