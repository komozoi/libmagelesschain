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

#ifndef LIBMAGELESSCHAIN_INDEXCONTAINERMANAGER_H
#define LIBMAGELESSCHAIN_INDEXCONTAINERMANAGER_H

#include <cstdint>
#include <mutex>
#include <string>

#include "IndexContainer.h"
#include "alloc/pointer.h"
#include "ds/HashMap.h"

/*
 * Pool of IndexContainer files, addressed by a single `containerId`.
 *
 * Layout on disk:
 *   <indexesDir>/<containerId>.bin
 *
 * Each container is a packed FreeSpaceFile that may hold payloads from
 * many different indexes side-by-side.  Containers are bounded by
 * IndexContainer::MAX_CONTAINER_BYTES; the manager picks a destination
 * container at write time such that the post-write file size stays
 * within that cap, and creates a new container otherwise.
 *
 * Selection policy on write:
 *   - Walk known containers in containerId order; pick the first one
 *     whose `approximateUsedBytes() + payload.size()` fits under
 *     MAX_CONTAINER_BYTES.
 *   - If none fits, create a new container with a fresh id.
 *
 * Concurrency: the manager itself uses a mutex to serialize container
 * selection/creation only.  The actual write into the chosen container
 * is serialized by the container's own internal mutex; two writes that
 * resolve to different containers may proceed in parallel.
 */
class IndexContainerManager {
public:
	/*
	 * Result of a successful payload write: the container the payload
	 * landed in, the offset within that container, and the byte length
	 * the caller asked to write.  These three numbers are exactly the
	 * (containerId, byteOffset, byteLength) fields of a SegmentLocator,
	 * so the caller can fill those directly.
	 */
	struct WriteResult {
		uint64_t containerId;
		uint64_t offset;
		uint64_t length;
	};

	explicit IndexContainerManager(const std::string& indexesDir);

	/*
	 * Choose a container that can fit `payload` under
	 * MAX_CONTAINER_BYTES, write the payload, and return its location.
	 */
	WriteResult write(const Bytestring& payload);

	/*
	 * Memory-map `length` bytes at `offset` within the container
	 * identified by `containerId`.  Opens the container lazily if it is
	 * not already cached.  The PayloadView's MmapHandle keeps the
	 * mapping alive even if the manager evicts the container handle.
	 */
	IndexContainer::PayloadView mmapPayload(uint64_t containerId, uint64_t offset, uint64_t length);

	/*
	 * Free a region inside the named container.  Used by compaction
	 * after a merged segment supersedes its inputs.
	 */
	void freeRegion(uint64_t containerId, uint64_t offset, uint64_t length);

	/*
	 * Number of distinct container files this manager has on disk
	 * (loaded lazily so this also represents what it knows about).
	 */
	int containerCount();

private:
	IndexContainer* getOrOpen(uint64_t containerId);
	uint64_t allocateNewContainerId();
	void scanExistingContainers();

	std::string indexesDir;
	std::mutex selectMutex;
	// Open container cache, keyed by containerId.  Containers are owned
	// by sp<> so concurrent readers still hold their mapping even if a
	// future eviction policy drops the handle.
	HashMap<uint64_t, sp<IndexContainer>> containers;
	// Highest containerId we know exists on disk (whether or not it is
	// currently open).  New container ids are allocated as this + 1.
	uint64_t maxKnownContainerId;
	// Set true after the first scanExistingContainers() so we never
	// re-scan the directory; the manager is the only writer.
	bool scanned;
};

#endif //LIBMAGELESSCHAIN_INDEXCONTAINERMANAGER_H
