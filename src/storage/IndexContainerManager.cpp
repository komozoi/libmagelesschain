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

#include "IndexContainerManager.h"

#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>

#include "fs/FdHandle.h"

IndexContainerManager::IndexContainerManager(const std::string& indexesDir)
	: indexesDir(indexesDir), containers(64), maxKnownContainerId(0),
	  containerMetaTree(nullptr) {
	mkdir(indexesDir.c_str(), 0770);

	std::string metaPath = indexesDir + "/containers.bin";
	FdHandle metaFile = FdHandle::open(metaPath.c_str(), O_RDWR | O_CREAT, 0660);
	containerMetaTree = new BTree<ContainerMetaEntry, 31>(std::move(metaFile), 0,
		ContainerMetaEntry::compare);

	loadKnownContainers();
}

IndexContainerManager::~IndexContainerManager() {
	delete containerMetaTree;
}

/*
 * Iterate the container metadata BTree in ascending containerId order
 * and pre-open every container so writes can pick a destination
 * without paying a syscall-per-open cost.  Container count is bounded
 * by the 2 GiB-per-container cap so this is small even for long-running
 * chains.  No filesystem directory scan is involved; the BTree is the
 * authoritative list of known containers.
 */
void IndexContainerManager::loadKnownContainers() {
	ContainerMetaEntry key = {};
	key.containerId = 0;
	while (containerMetaTree->findNext(key)) {
		if (key.tombstone == 0) {
			if (key.containerId > maxKnownContainerId)
				maxKnownContainerId = key.containerId;
			getOrOpen(key.containerId);
		}
		if (key.containerId == UINT64_MAX) break;
		ContainerMetaEntry next = {};
		next.containerId = key.containerId + 1;
		key = next;
	}
}

uint64_t IndexContainerManager::allocateNewContainerId() {
	// Caller holds selectMutex.  Persist the new id in the metadata
	// BTree so future reopens rediscover it without a directory scan.
	maxKnownContainerId++;
	ContainerMetaEntry e = {};
	e.containerId = maxKnownContainerId;
	containerMetaTree->insert(e);
	return maxKnownContainerId;
}

IndexContainer* IndexContainerManager::getOrOpen(uint64_t containerId) {
	// Caller holds selectMutex (or this is the constructor's pre-open path).
	if (containers.hasKey(containerId))
		return containers.get(containerId).get();

	std::string path = indexesDir + "/" + std::to_string(containerId) + ".bin";
	sp<IndexContainer> c = sp<IndexContainer>::create(path);
	IndexContainer* raw = c.get();
	containers.put(containerId, std::move(c));
	return raw;
}

IndexContainerManager::WriteResult IndexContainerManager::write(const Bytestring& payload) {
	if (payload.size() == 0)
		throw std::invalid_argument("IndexContainerManager::write: empty payload");
	uint64_t needed = (uint64_t)payload.size();

	uint64_t chosenId = 0;
	IndexContainer* chosen = nullptr;
	{
		std::lock_guard _(selectMutex);

		// Walk known containers in id order; first one that can fit
		// wins.  ContainerId 1 is the oldest, larger ids are newer,
		// so we naturally pack into the lowest-numbered file with
		// room, which keeps the working set small on disk.
		for (uint64_t id = 1; id <= maxKnownContainerId; ++id) {
			IndexContainer* c = getOrOpen(id);
			if (!c) continue;
			uint64_t used = c->approximateUsedBytes();
			if (used + needed <= IndexContainer::MAX_CONTAINER_BYTES) {
				chosenId = id;
				chosen = c;
				break;
			}
		}

		if (!chosen) {
			chosenId = allocateNewContainerId();
			chosen = getOrOpen(chosenId);
		}
	}

	// Container's own mutex serializes the actual write; two writes
	// resolving to different containers proceed in parallel here.
	uint64_t offset = chosen->writePayload(payload);

	WriteResult r;
	r.containerId = chosenId;
	r.offset = offset;
	r.length = needed;
	return r;
}

IndexContainer::PayloadView IndexContainerManager::mmapPayload(uint64_t containerId, uint64_t offset, uint64_t length) {
	IndexContainer* c;
	{
		std::lock_guard _(selectMutex);
		c = getOrOpen(containerId);
		if (containerId > maxKnownContainerId) maxKnownContainerId = containerId;
	}
	return c->mmapPayload(offset, length);
}

void IndexContainerManager::freeRegion(uint64_t containerId, uint64_t offset, uint64_t length) {
	IndexContainer* c;
	{
		std::lock_guard _(selectMutex);
		c = getOrOpen(containerId);
	}
	c->freeRegion(offset, length);
}

int IndexContainerManager::containerCount() {
	std::lock_guard _(selectMutex);
	return (int)maxKnownContainerId;
}
