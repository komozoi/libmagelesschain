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

#ifndef LIBMAGELESSCHAIN_BLOCKCHAININDEX_H
#define LIBMAGELESSCHAIN_BLOCKCHAININDEX_H

#include <cstdint>
#include "ds/Bytestring.h"
#include "ds/ArrayList.h"

/*
 * Base interface for an application-defined blockchain index.
 *
 * Indexes own the semantics of how chain state is materialized on disk and
 * how it is queried.  Every index in this library is segment-based: state
 * is written out as discrete segments, each covering a contiguous block
 * range, and the library orchestrates segment storage (locations,
 * containers, catalog entries, checksums).  The encoding of each segment's
 * payload and the meaning of the data it holds are entirely application
 * defined.
 *
 * The Phase 1 refactor introduces the interface only; the orchestration
 * that actually drives writeSegment/readSegment/mergeSegments is not yet
 * wired up.  Subclasses can implement these as TODOs until the storage
 * layer lands.
 */
class BlockchainIndex {
public:
	/*
	 * Returns the segment encoding version.  When the library reads back an
	 * older segment that this index cannot decode, the affected block range
	 * is rebuilt from the journal.
	 */
	virtual uint16_t encodingVersion() const = 0;

	/*
	 * Encode the index's state for [blockRangeStart, blockRangeEnd] as a
	 * payload byte string.  The library writes this verbatim, prefixed with
	 * its own framing.
	 */
	virtual Bytestring writeSegment(uint64_t blockRangeStart, uint64_t blockRangeEnd) const = 0;

	/*
	 * Decode a previously written segment payload back into in-memory state.
	 * If the encoding version recorded in the catalog is one this index
	 * cannot decode, return false so the library can mark the block range
	 * for rebuild from the journal.
	 */
	virtual bool readSegment(const Bytestring& payload, uint16_t encodingVersionOnDisk, uint64_t blockRangeStart, uint64_t blockRangeEnd) = 0;

	/*
	 * Merge several segments into one.  The library picks which segments to
	 * merge based on size and count thresholds, hands the payloads to this
	 * method, and gets back the merged payload to write as a new segment.
	 */
	virtual Bytestring mergeSegments(const ArrayList<Bytestring>& payloads, const ArrayList<uint16_t>& encodingVersionsOnDisk) const = 0;

	virtual ~BlockchainIndex() = default;
};

#endif //LIBMAGELESSCHAIN_BLOCKCHAININDEX_H
