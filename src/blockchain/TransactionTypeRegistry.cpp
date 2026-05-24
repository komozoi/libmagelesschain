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

#include "TransactionTypeRegistry.h"
#include "Transaction.h"
#include "fs/FdHandle.h"

#include <stdexcept>

sp<Transaction> TransactionTypeRegistry::read(MmapHandle* src) const {
	uint8_t typeId;
	if (src->read(typeId) != sizeof(uint8_t))
		throw std::out_of_range("Failed to read transaction type");
	FactoryFunc f = getFactory(typeId);
	if (!f) return sp<Transaction>();
	return f(src);
}
