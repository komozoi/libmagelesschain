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
 * Lightweight interface contract tests for BlockchainIndex.  In Phase 1
 * the segment-lifecycle methods are not yet driven by the library, but
 * implementations must still satisfy the contract: encodingVersion() is
 * stable, and the segment hooks are at least callable without crashing.
 *
 * Once the storage layer lands (Phase 2+) these will be expanded into
 * round-trip and merge correctness tests; for now they exist as a known-
 * good seed so the orchestration code has something to call.
 */

TEST(BlockchainIndexTest, EncodingVersionIsStable) {
	TestSumIndex idx;
	EXPECT_EQ(idx.encodingVersion(), idx.encodingVersion());
}

TEST(BlockchainIndexTest, WriteAndReadSegmentSucceedsWithMatchingVersion) {
	TestSumIndex idx;
	Bytestring payload = idx.writeSegment(0, 9);
	// readSegment with the same encoding version this index reports must
	// not reject the segment.
	EXPECT_TRUE(idx.readSegment(payload, idx.encodingVersion(), 0, 9));
}

TEST(BlockchainIndexTest, MergeSegmentsCallable) {
	TestSumIndex idx;
	ArrayList<Bytestring> payloads;
	payloads.add(idx.writeSegment(0, 4));
	payloads.add(idx.writeSegment(5, 9));
	ArrayList<uint16_t> versions;
	versions.add(idx.encodingVersion());
	versions.add(idx.encodingVersion());
	// Phase 1 returns an empty Bytestring placeholder; we just want to
	// ensure the contract is callable end to end.
	Bytestring merged = idx.mergeSegments(payloads, versions);
	(void)merged;
	SUCCEED();
}
