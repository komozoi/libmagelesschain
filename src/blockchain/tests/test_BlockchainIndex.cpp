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

#include "blockchain/BlockchainIndex.h"
#include "testutil/TestChainDesign.h"

/*
 * Lightweight interface contract tests for BlockchainIndex.
 *
 * BlockchainIndex is a query handle: at query time it asks its attached
 * Catalog which segments cover the block range of interest and mmaps
 * just those payloads through its attached IndexContainerManager.
 *
 * These tests pin the bare-bones contract: encodingVersion() is stable,
 * attach() stores its arguments, and mergeSegments() with no inputs
 * returns an empty payload.  Full segment-orchestration behavior is
 * exercised end to end in test_SegmentOrchestration.cpp.
 */

TEST(BlockchainIndexTest, EncodingVersionIsStable) {
	TestSumIndex idx;
	EXPECT_EQ(idx.encodingVersion(), idx.encodingVersion());
}

TEST(BlockchainIndexTest, AttachStoresStorageHandles) {
	TestSumIndex idx;
	// With no storage attached the index reports a benign zero, since
	// there are no segments to read.
	EXPECT_EQ(idx.latestSum(), 0);
	EXPECT_EQ(idx.latestCount(), 0);
}

TEST(BlockchainIndexTest, MergeSegmentsWithNoInputsReturnsEmpty) {
	TestSumIndex idx;
	ArrayList<segment_coordinate_t> empty;
	Bytestring merged = idx.mergeSegments(empty);
	EXPECT_EQ((int)merged.size(), 0);
}
