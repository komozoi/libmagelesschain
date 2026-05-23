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

#ifndef LIBMAGELESSCHAIN_BLOCKCHAINFRONTEND_H
#define LIBMAGELESSCHAIN_BLOCKCHAINFRONTEND_H

#include "BlockchainBackend.h"


/*
 * The blockchain frontend and backend are separated to abstract certain details.
 * The frontend works more like a database, allowing quick read/write access and abstraction of
 * transactions with the tradeoff that some data may not be final or completely visible yet.
 * The backend handles writing to disk and stores persistent state, with the tradeoff that state is
 * processed in discreet blocks, which may not be fast enough for real-time data use.
 */
class BlockchainFrontend {
public:
	BlockchainFrontend(BlockchainBackend& backend);

	// Only one global instance of this should really exist
	// Perspectives could be created for specific purposes, but those
	// do not involve copy/move.
	BlockchainFrontend(BlockchainFrontend const&) = delete;
	BlockchainFrontend(BlockchainFrontend&&) = delete;

	int getBlockHeight() const { return backend.getBlockHeight(); }
	int getMempoolSize() const { return mempool.size(); }

	void sendTransaction(const sp<Transaction>& transaction) { mempool.add(transaction); }

private:
	BlockchainBackend& backend;
	ArrayList<sp<Transaction>> mempool;
};


#endif //LIBMAGELESSCHAIN_BLOCKCHAINFRONTEND_H