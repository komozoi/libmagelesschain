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
#include "StateOverride.h"


class BlockchainBackend;


/**
 * @class MEVBuilder
 *
 * @brief Builds blocks by exploring candidate orderings of mempool
 *        transactions to maximize their total computed value.
 *
 * Value is what the application optimizes for: transaction fees, ordering
 * fairness, data completeness, success rate, anything Transaction::computeValue
 * returns.  The builder treats it as an opaque float to maximize.
 *
 * Approach:
 *  - Start from a fresh StateOverride representing the committed chain state
 *    (built via backend.newStateOverride()).  Forks of this override use
 *    sp<T> CoW so untouched override families are shared across candidates.
 *  - Greedily pick the highest-value transaction at each step, re-evaluating
 *    computeValue against the simulated state after each pick.  This
 *    correctly handles transactions whose value depends on the state
 *    changes of earlier transactions in the block.
 *  - Optimization loop runs until the deadline (not yet implemented; placeholder for
 *    future swap/2-opt style improvement passes).
 *
 * Returned transactions are in execution order.  Selected transactions are
 * removed from the supplied mempool.
 */
class MEVBuilder {
public:
	MEVBuilder(BlockchainBackend& backend);

	/**
	 * Build a block by selecting transactions from the mempool that
	 * optimize for total value.
	 *
	 * This method follows this process:
	 * 1. Select the top maxTransactions transactions from the mempool, prioritizing those that optimize for the defined "value".
	 * 2. While time remains and further optimization is possible, try to reorder or change transactions to improve the total value.
	 * 3. Remove selected transactions from the mempool.
	 * 4. Return the selected transactions in the order that they should be executed.
	 *
	 * @param mempool      Candidate transactions.  Selected ones are removed.
	 * @param maxTransactions  Upper bound on transactions per block.
	 * @param deadline     Wall-clock millis at which to stop the optimization
	 *                     loop.  Pass 0 to skip the optimization loop entirely.
	 * @return  Selected transactions in execution order.
	 */
	ArrayList<sp<Transaction>> buildBlock(ArrayList<sp<Transaction>>& mempool,
	                                       uint16_t maxTransactions,
	                                       uint64_t deadline) const;

private:
	BlockchainBackend& backend;
};


#endif //LIBMAGELESSCHAIN_MEVBUILDER_H
