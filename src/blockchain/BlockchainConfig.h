/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-22
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

#ifndef LIBMAGELESSCHAIN_BLOCKCHAINCONFIG_H
#define LIBMAGELESSCHAIN_BLOCKCHAINCONFIG_H

#include <cstdint>

struct BlockchainConfig {
	uint32_t targetBlockTimeMs = 60000;
	uint32_t targetThroughput = 180;

	/*
	 * Segment count above which the backend will trigger a compaction on
	 * an index instance.  Applications typically tune this to
	 * some multiple of the number of available CPU threads so
	 * background merges parallelize cleanly.  When the segment count
	 * exceeds the threshold, the two smallest mergeable segments (each
	 * under 500 MiB per the proposal) are merged into one.
	 */
	uint32_t maxSegmentsPerIndex = 64;

	/*
	 * Per-segment size limit beyond which a segment is no longer eligible
	 * for merging.  512 MiB per the concrete proposal.
	 *
	 * This number is downstream of the maximum file size of ~2GiB.
	 * Merging two 512MiB segments is not going to reach 2GiB, but
	 * merging two 1GiB segments might.
	 */
	uint64_t maxMergeableSegmentBytes = 512ULL * 1024 * 1024;
};

#endif //LIBMAGELESSCHAIN_BLOCKCHAINCONFIG_H
