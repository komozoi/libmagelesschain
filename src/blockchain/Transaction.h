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

#ifndef LIBMAGELESSCHAIN_TRANSACTION_H
#define LIBMAGELESSCHAIN_TRANSACTION_H

#include "alloc/pointer.h"
#include "fs/FdHandle.h"


class BlockchainStateSnapshot;

class Transaction {
public:
	virtual bool verify(BlockchainStateSnapshot& snapshot) const = 0;
	virtual bool apply(BlockchainStateSnapshot& snapshot) const = 0;
	virtual float computeValue(BlockchainStateSnapshot& snapshot) const = 0;

	virtual void write(MmapHandle* dst) const = 0;
	virtual size_t size() const = 0;
	virtual uint8_t getTypeId() const = 0;
	virtual uint64_t getTimestamp() const = 0;

	virtual ~Transaction() = default;

	typedef sp<Transaction> (*FactoryFunc)(MmapHandle*);
	static void registerType(uint8_t typeId, FactoryFunc factory);
	static sp<Transaction> read(MmapHandle* src);
};


#endif //LIBMAGELESSCHAIN_TRANSACTION_H
