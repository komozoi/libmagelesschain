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
#include <mutex>
#include <string>

#include "fs/FreeSpaceFile.h"
#include "fs/FdHandle.h"
#include "ds/Bytestring.h"

/*
 * A single on-disk file holding segment payloads.  Containers are
 * pure storage: a container is keyed only by `containerId` and may
 * hold payloads belonging to many different indexes side-by-side.
 *
 * Region management is delegated to libexcessive's FreeSpaceFile, so
 * regions freed by compaction can be reused by subsequent writes inside
 * the same container.  The hard cap per container is the per-container
 * 2 GiB ceiling (`MAX_CONTAINER_BYTES`), kept below the 2 GiB signed
 * file-size limit common to mmap/Java consumers.
 */
class IndexContainer {
public:
	/*
	 * Per-container 2047 MiB ceiling.  IndexContainerManager picks a
	 * container such that approximateUsedBytes() + payload.size() stays
	 * under this number.
	 */
	static constexpr uint64_t MAX_CONTAINER_BYTES = 2047ull * 1024ull * 1024ull;

	/*
	 * Open or create a container file at the given path.
	 */
	explicit IndexContainer(const std::string& filePath);

	/*
	 * Allocate a region of `payload.size()` bytes via FreeSpaceFile,
	 * write the payload to it, and return the byte offset where the
	 * payload lives.  Caller is responsible for not selecting a
	 * container that would exceed MAX_CONTAINER_BYTES.
	 *
	 * This method takes the container's internal mutex; concurrent
	 * writes to the same container are serialized.
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
	 * Memory-map `length` bytes at `offset`.  Multiple readers may map
	 * concurrently; the underlying FdHandle's mmap call is not guarded
	 * by the container mutex.
	 */
	PayloadView mmapPayload(uint64_t offset, uint64_t length);

	/*
	 * Mark the region [offset, offset+length) as free, so future writes
	 * may reuse it.
	 */
	void freeRegion(uint64_t offset, uint64_t length);

	/*
	 * Approximate end-of-allocated-data offset.  Cheap (file size); does
	 * not subtract freed regions, so it is an upper bound on used bytes.
	 * Used by IndexContainerManager for the per-container size cap.
	 */
	uint64_t approximateUsedBytes();

private:
	FreeSpaceFile file;
	std::mutex writeMutex;
};

#endif //LIBMAGELESSCHAIN_INDEXCONTAINER_H
