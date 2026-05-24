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

#include <thread>
#include <atomic>
#include <mutex>

#include "BlockchainBackend.h"
#include "BlockchainConfig.h"
#include "MEVBuilder.h"
#include "StateOverride.h"


/*
 * Application-facing handle to the chain.  Owns the mempool, the speculative
 * StateOverride that reflects the speculative effect of every mempool
 * transaction on top of the committed chain, and the background block-
 * builder thread.
 *
 * The blockchain frontend and backend are separated to abstract certain details.
 * The frontend works more like a database, allowing quick read/write access and abstraction of
 * transactions with the tradeoff that some data may not be final or completely visible yet.
 * The backend handles writing to disk and stores persistent state, with the tradeoff that state is
 * processed in discreet blocks, which may not be fast enough for real-time data use.
 *
 * The frontend never opens index, segment, container, or catalog files.
 * Its only filesystem responsibility is mempool.bin in the backend's data
 * directory.
 */
class BlockchainFrontend {
public:
	BlockchainFrontend(BlockchainBackend& backend, BlockchainConfig config = {});
	~BlockchainFrontend();

	// Only one global instance of this should really exist
	// Perspectives could be created for specific purposes, but those
	// do not involve copy/move.
	BlockchainFrontend(BlockchainFrontend const&) = delete;
	BlockchainFrontend(BlockchainFrontend&&) = delete;

	int getBlockHeight() const { return backend.getBlockHeight(); }
	int getMempoolSize() const { return mempool.size(); }

	/*
	 * Submit a transaction to the mempool.  The frontend's speculative
	 * StateOverride is updated immediately so that subsequent reads see
	 * the pending changes.
	 */
	void sendTransaction(const sp<Transaction>& transaction);

	sp<Transaction> getTransactionById(uint64_t id) const;
	ArrayList<sp<Transaction>> getTransactionsByTimeWindow(uint64_t startMillis, uint64_t endMillis);

	/*
	 * Speculative state: the committed chain plus every transaction
	 * currently in the mempool, applied in mempool order.  Tests and
	 * applications read through this to see the chain "as it would be"
	 * if all pending transactions committed.
	 */
	sp<StateOverride> getState() const { return state; }

private:
	void blockBuilderLoop();
	void saveMempool();
	void loadMempool();
	void rebuildSpeculativeState();

	BlockchainBackend& backend;
	BlockchainConfig config;
	MEVBuilder mevBuilder;
	sp<StateOverride> state;
	ArrayList<sp<Transaction>> mempool;
	mutable std::mutex mempoolMutex;

	std::thread builderThread;
	std::atomic<bool> running;
};


#endif //LIBMAGELESSCHAIN_BLOCKCHAINFRONTEND_H
