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

#ifndef LIBMAGELESSCHAIN_SEGMENTLOCATOR_H
#define LIBMAGELESSCHAIN_SEGMENTLOCATOR_H

#include <cstdint>

/*
 * Catalog entry describing the location and metadata of a single on-disk
 * segment.  A segment is the unit of persistent index state: one segment
 * per (index instance, block range) when the override family for that
 * index produced non-empty pending changes for the committed block.
 *
 * The struct is a fixed-size POD so it can be stored directly as the
 * element type of libexcessive's BTree, which requires a fixed sizeof for
 * its node layout.
 *
 * Persistence of the type identifier:
 *   `persistentTypeId` is assigned by the backend at construction time
 *   from the registration order of indexes within the active ChainDesign.
 *   The same order must be reproduced across restarts (this is the same
 *   registration-order contract that backs backend.index<T>(id)).  This
 *   is intentionally not the in-process TypeKey<T>() pointer, which is
 *   not stable across processes.
 */
struct SegmentLocator {
	// Primary key fields (used by compare):
	uint16_t persistentTypeId;   // registration order within BackendRegistry
	uint8_t  instanceId;
	uint8_t  reserved1;
	uint16_t encodingVersion;
	uint16_t reserved2;
	uint32_t mergeGeneration;
	uint64_t blockRangeStart;    // inclusive
	uint64_t blockRangeEnd;      // inclusive

	// Physical location:
	uint64_t containerId;
	uint64_t byteOffset;
	uint64_t byteLength;

	// Integrity:
	uint64_t checksum;           // excessiveFastHash over the raw payload bytes

	/*
	 * Lexicographic ordering by (persistentTypeId, instanceId,
	 * blockRangeStart, mergeGeneration).  Including mergeGeneration last
	 * keeps merge-output entries adjacent to the inputs they replace and
	 * lets a range scan see them deterministically.
	 */
	static int compare(const SegmentLocator& a, const SegmentLocator& b) {
		if (a.persistentTypeId < b.persistentTypeId) return -1;
		if (a.persistentTypeId > b.persistentTypeId) return 1;
		if (a.instanceId < b.instanceId) return -1;
		if (a.instanceId > b.instanceId) return 1;
		if (a.blockRangeStart < b.blockRangeStart) return -1;
		if (a.blockRangeStart > b.blockRangeStart) return 1;
		if (a.mergeGeneration < b.mergeGeneration) return -1;
		if (a.mergeGeneration > b.mergeGeneration) return 1;
		return 0;
	}
};

#endif //LIBMAGELESSCHAIN_SEGMENTLOCATOR_H
