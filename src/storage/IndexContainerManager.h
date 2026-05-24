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
#include <string>

#include "IndexContainer.h"
#include "alloc/pointer.h"
#include "ds/HashMap.h"

/*
 * Lazy-opening cache of IndexContainer instances keyed by
 * (persistentTypeId, instanceId, containerId).
 *
 * Layout on disk:
 *   <dataDir>/indexes/<persistentTypeId>-<instanceId>-<containerId>.bin
 *
 * Phase 2 keeps one container per (typeId, instanceId), i.e. containerId
 * always 0.  Size-threshold rollover is supported by the storage primitives
 * (FreeSpaceFile) but not yet driven by the backend; the API is
 * intentionally containerId-aware so the rollover hook is a single-line
 * change in the future.
 */
class IndexContainerManager {
public:
	explicit IndexContainerManager(const std::string& indexesDir);

	/*
	 * Get (or open lazily) the container for the given coordinates.
	 * The returned pointer is owned by this manager and remains valid for
	 * the manager's lifetime.
	 */
	IndexContainer* get(uint16_t persistentTypeId, uint8_t instanceId, uint64_t containerId);

private:
	std::string indexesDir;
	// Key encodes (persistentTypeId << 32) | (instanceId << 24) | containerId.
	HashMap<uint64_t, sp<IndexContainer>> containers;
};

#endif //LIBMAGELESSCHAIN_INDEXCONTAINERMANAGER_H
