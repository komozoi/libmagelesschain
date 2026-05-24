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

#ifndef LIBMAGELESSCHAIN_INDEXCONTAINER_H
#define LIBMAGELESSCHAIN_INDEXCONTAINER_H

#include <cstdint>
#include <string>

#include "fs/FreeSpaceFile.h"
#include "fs/FdHandle.h"
#include "ds/Bytestring.h"

/*
 * A single on-disk file holding one or more segment payloads belonging to
 * one index instance.  The library decides where the payload bytes live;
 * the index controls only what is inside them.
 *
 * Region management is delegated to libexcessive's FreeSpaceFile, so freed
 * regions (from merged-out segments) can be reused by future writes inside
 * the same container.
 *
 * One container file per (persistentTypeId, instanceId, containerId),
 * so segments from different indexes are siloed in different files.
 * The current implementation keeps a single container per instance
 * (containerId 0); the API is containerId-aware so size-threshold
 * rollover can plug in by writing into a new containerId.
 */
class IndexContainer {
public:
	/*
	 * Open or create a container file at the given path.
	 */
	explicit IndexContainer(const std::string& filePath);

	/*
	 * Allocate a region of `payload.size()` bytes, write the payload to it,
	 * and return the byte offset where the payload lives.
	 */
	uint64_t writePayload(const Bytestring& payload);

	/*
	 * View over a memory-mapped segment payload.  The MmapHandle is held
	 * by-value so the mapping stays alive as long as the view does;
	 * `data` already accounts for any page-alignment offset.  Indexes use
	 * this to random-access segment payloads without copying.
	 */
	struct PayloadView {
		MmapHandle handle;
		const uint8_t* data = nullptr;
		uint64_t length = 0;
	};

	/*
	 * Memory-map `length` bytes at `offset`.  The returned PayloadView's
	 * `data` pointer is valid for `length` bytes and lives as long as the
	 * view does.
	 */
	PayloadView mmapPayload(uint64_t offset, uint64_t length);

	/*
	 * Mark the region [offset, offset+length) as free, so future writes
	 * may reuse it.
	 */
	void freeRegion(uint64_t offset, uint64_t length);

	/*
	 * Approximate end-of-allocated-data offset, used for size-threshold
	 * rollover decisions.  Cheap: the current file size after the last
	 * allocation.
	 */
	uint64_t approximateUsedBytes();

private:
	FreeSpaceFile file;
};

#endif //LIBMAGELESSCHAIN_INDEXCONTAINER_H
