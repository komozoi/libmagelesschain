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


class BlockchainStateSnapshot;

class Transaction {
public:
	virtual bool verify(BlockchainStateSnapshot& snapshot) const = 0;
	virtual bool write(BlockchainStateSnapshot& snapshot) const = 0;
	virtual float computeValue(BlockchainStateSnapshot& snapshot) const = 0;

	virtual void write(void* dst) const = 0;

	virtual ~Transaction() = default;
};


#endif //LIBMAGELESSCHAIN_TRANSACTION_H
