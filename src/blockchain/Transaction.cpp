/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-15
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

#include "Transaction.h"

#include "ds/HashMap.h"

#include <mutex>


static HashMap<uint32_t, Transaction::FactoryFunc>& getFactories() {
	static HashMap<uint32_t, Transaction::FactoryFunc> factories(16);
	return factories;
}

static std::mutex& getFactoriesMutex() {
	static std::mutex mutex;
	return mutex;
}

void Transaction::registerType(const uint8_t typeId, const FactoryFunc factory) {
	std::lock_guard _(getFactoriesMutex());
	getFactories().put(typeId, factory);
}

sp<Transaction> Transaction::read(MmapHandle* src) {
	uint8_t typeId;
	if (src->read(typeId) != sizeof(uint8_t))
		throw std::out_of_range("Failed to read transaction type");

	std::lock_guard _(getFactoriesMutex());
	const FactoryFunc* factory = getFactories().getPtr(typeId);
	if (!factory) return nullptr;
	return (*factory)(src);
}
