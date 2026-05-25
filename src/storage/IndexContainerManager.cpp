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

#include <dirent.h>
#include <stdexcept>
#include <sys/stat.h>

IndexContainerManager::IndexContainerManager(const std::string& indexesDir)
	: indexesDir(indexesDir), containers(64), maxKnownContainerId(0), scanned(false) {
	mkdir(indexesDir.c_str(), 0770);
	scanExistingContainers();
}

void IndexContainerManager::scanExistingContainers() {
	if (scanned) return;
	scanned = true;

	DIR* dir = opendir(indexesDir.c_str());
	if (!dir) return;
	struct dirent* ent;
	while ((ent = readdir(dir)) != nullptr) {
		std::string name(ent->d_name);
		if (name.size() < 5) continue;
		if (name.compare(name.size() - 4, 4, ".bin") != 0) continue;
		// Try to parse the base name as an unsigned integer.  We treat
		// any non-numeric filename as foreign and skip it; only files we
		// created (named `<id>.bin`) are tracked here.
		std::string base = name.substr(0, name.size() - 4);
		uint64_t id = 0;
		bool ok = !base.empty();
		for (char c : base) {
			if (c < '0' || c > '9') { ok = false; break; }
			id = id * 10 + (uint64_t)(c - '0');
		}
		if (!ok) continue;
		if (id > maxKnownContainerId) maxKnownContainerId = id;
		// Open lazily; we just need to know it exists for selection.
		// getOrOpen will do the real work when something needs to use it.
		(void)id;
	}
	closedir(dir);

	// Pre-open every known container so selection can read sizes
	// without paying a syscall-per-write open cost.  Container count is
	// small (the 2 GiB cap means a long-running chain holds dozens to
	// hundreds, not millions).
	for (uint64_t id = 1; id <= maxKnownContainerId; ++id) {
		std::string path = indexesDir + "/" + std::to_string(id) + ".bin";
		struct stat st;
		if (stat(path.c_str(), &st) == 0) {
			getOrOpen(id);
		}
	}
}

uint64_t IndexContainerManager::allocateNewContainerId() {
	// Caller holds selectMutex.
	maxKnownContainerId++;
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
