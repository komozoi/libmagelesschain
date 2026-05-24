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

#include <sys/stat.h>

static uint64_t packKey(uint16_t persistentTypeId, uint8_t instanceId, uint64_t containerId) {
	// Containers are uniquely keyed by (typeId, instanceId, containerId); we
	// pack these into a single uint64_t so HashMap can key on a value.
	return ((uint64_t)persistentTypeId << 48)
		| ((uint64_t)instanceId << 40)
		| (containerId & 0x000000FFFFFFFFFFULL);
}

IndexContainerManager::IndexContainerManager(const std::string& indexesDir)
	: indexesDir(indexesDir), containers(64) {
	mkdir(indexesDir.c_str(), 0770);
}

IndexContainer* IndexContainerManager::get(uint16_t persistentTypeId, uint8_t instanceId, uint64_t containerId) {
	uint64_t key = packKey(persistentTypeId, instanceId, containerId);
	if (containers.hasKey(key))
		return containers.get(key).get();

	std::string filePath = indexesDir + "/" + std::to_string(persistentTypeId)
		+ "-" + std::to_string((int)instanceId) + "-" + std::to_string(containerId) + ".bin";
	sp<IndexContainer> container = sp<IndexContainer>::create(filePath);
	IndexContainer* raw = container.get();
	containers.put(key, std::move(container));
	return raw;
}
