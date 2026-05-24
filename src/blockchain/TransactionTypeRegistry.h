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

#ifndef LIBMAGELESSCHAIN_TRANSACTIONTYPEREGISTRY_H
#define LIBMAGELESSCHAIN_TRANSACTIONTYPEREGISTRY_H

#include <cstdint>

#include "alloc/pointer.h"
#include "ds/HashMap.h"

class Transaction;
class MmapHandle;

/*
 * Per-backend registry mapping a uint8_t typeId to a factory function
 * that deserializes a Transaction subclass from a memory-mapped byte
 * stream.
 */
class TransactionTypeRegistry {
public:
	typedef sp<Transaction> (*FactoryFunc)(MmapHandle*);

	TransactionTypeRegistry() : factories(16) {}

	void registerType(uint8_t typeId, FactoryFunc factory) {
		factories.put(typeId, factory);
	}

	FactoryFunc getFactory(uint8_t typeId) const {
		const FactoryFunc* f = factories.getPtr(typeId);
		return f ? *f : nullptr;
	}

	sp<Transaction> read(MmapHandle* src) const;

private:
	HashMap<uint8_t, FactoryFunc> factories;
};

#endif //LIBMAGELESSCHAIN_TRANSACTIONTYPEREGISTRY_H
