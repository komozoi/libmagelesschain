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


class StateOverride;

/*
 * Application-defined transaction.  Subclasses define their own payload
 * fields, serialization, validity rules, value scoring, and apply logic.
 *
 * Lifecycle:
 *   - sendTransaction places the transaction into the mempool.
 *   - The frontend applies the transaction to its speculative StateOverride
 *     immediately so reads through the override reflect pending changes.
 *   - MEVBuilder explores candidate orderings, calling computeValue and
 *     apply against forked StateOverrides.
 *   - On commit, the backend applies the chosen ordering to a fresh
 *     override and seals each override family into a segment.
 *
 * Failed transactions (apply returns false) are still committed to the
 * journal so the block remains queryable, but they contribute no state
 * delta.
 */
class Transaction {
public:
	/*
	 * Validate that this transaction is well-formed against the chain
	 * state as seen through the supplied override.  Should not mutate
	 * the override.  Transactions that fail this should not be committed.
	 * Currently this isn't called, but it should be to prevent ill-formed
	 * trasnactions from being committed.
	 */
	virtual bool verify(const StateOverride& state) const = 0;

	/*
	 * Apply the transaction's state changes through the override's
	 * typed family accessors.  Return true on success, false on failure.
	 * Failed transactions are still committed to the journal but do not
	 * change chain state.
	 */
	virtual bool apply(StateOverride& state) const = 0;

	/*
	 * Score this transaction for MEV ordering against the supplied
	 * override.  Re-evaluated after each pick so that state-dependent
	 * value (e.g. "worth more if some other transaction has executed
	 * first") is respected.
	 */
	virtual float computeValue(const StateOverride& state) const = 0;

	/*
	 * Serialize this transaction (including its 1-byte type id prefix)
	 * into the destination.  read() in TransactionTypeRegistry expects
	 * the type id to be the first byte written.
	 */
	virtual void write(MmapHandle* dst) const = 0;

	virtual size_t size() const = 0;
	virtual uint8_t getTypeId() const = 0;
	virtual uint64_t getTimestamp() const = 0;

	virtual ~Transaction() = default;
};


#endif //LIBMAGELESSCHAIN_TRANSACTION_H
