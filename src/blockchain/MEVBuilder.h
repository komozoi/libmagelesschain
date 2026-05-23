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

#ifndef LIBMAGELESSCHAIN_MEVBUILDER_H
#define LIBMAGELESSCHAIN_MEVBUILDER_H

#include <alloc/pointer.h>
#include <ds/ArrayList.h>

#include "Transaction.h"
#include "BlockchainStateSnapshot.h"


class BlockchainBackend;


/**
 * @class MEVBuilder
 *
 * @brief Builds blocks to maximize transaction success and "profit", where
 *        profit is defined by data completeness, conciseness, and other factors.
 *
 * In traditional blockchains, MEV (Maximum Extractable Value) refers to the
 * practice of extracting value from transactions by strategically ordering
 * them within a block. In this case, value is flexibly defined by the transaction
 * subclass, so we can optimize for other metrics like success rate, priority, etc.
 *
 * This class provides logic to build blocks while optimizing for this "value"
 * defined by the subclass.
 *
 * One possible optimization strategy:
 *  - The ideal block includes as many transactions as possible, fails none of them,
 *    encodes the data as small as possible, and keeps it effectively indexed.
 *
 * These variables are affected by execution order and other details, so a MEV
 * builder is warranted.
 *
 * It is the job of the chain state and transaction failure logic to ensure that
 * data inconsistencies cannot be introduced, so this class does not worry about
 * these concerns.
 */
class MEVBuilder {
public:
	MEVBuilder(BlockchainBackend& backend);

	/**
	 * Builds a block by selecting transactions from the mempool that optimize for the defined "value".
	 *
	 * This method follows this process:
	 * 1. Select the top maxTransactions transactions from the mempool, prioritizing those that optimize for the defined "value".
	 * 2. While time remains and further optimization is possible, try to reorder or change transactions to improve the total value.
	 * 3. Remove selected transactions from the mempool.
	 * 4. Return the selected transactions in the order that they should be executed.
	 *
	 * @param mempool List of transactions to consider for inclusion in the block.  Selected transactions are removed from the list.
	 * @param maxTransactions Maximum number of transactions to include in the block
	 * @param deadline Timestamp by which the block must be built
	 * @return List of transactions included in the built block, in the order that they should be executed
	 */
	ArrayList<sp<Transaction>> buildBlock(ArrayList<sp<Transaction>>& mempool, uint16_t maxTransactions, uint64_t deadline) const;

private:
	BlockchainBackend& backend;
};


#endif //LIBMAGELESSCHAIN_MEVBUILDER_H