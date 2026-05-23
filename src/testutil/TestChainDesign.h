
/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-23
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

#ifndef LIBMAGELESSCHAIN_TESTCHAINDESIGN_H
#define LIBMAGELESSCHAIN_TESTCHAINDESIGN_H

#include "blockchain/Transaction.h"
#include "blockchain/BlockchainStateSnapshot.h"


class TestState : public BlockchainStateSnapshot {
public:
	int sum = 0;
	int count = 0;
	TestState(BlockchainBackend& backend, long blockHeight);
};

class TestTransaction : public Transaction {
public:
	int value;
	uint64_t timestamp;
	int id;

	TestTransaction(int val = 1, int id = 0);

	bool verify(BlockchainStateSnapshot& snapshot) const override;
	bool apply(BlockchainStateSnapshot& snapshot) const override;
	float computeValue(BlockchainStateSnapshot& snapshot) const override;

	uint8_t getTypeId() const override;
	uint64_t getTimestamp() const override;

	void write(MmapHandle* dst) const override;

	size_t size() const override;

	static sp<Transaction> createFromMmap(MmapHandle* src);
};

#endif //LIBMAGELESSCHAIN_TESTCHAINDESIGN_H
