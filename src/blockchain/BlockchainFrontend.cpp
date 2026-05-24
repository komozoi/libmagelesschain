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

#include "BlockchainFrontend.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "universaltime.h"

BlockchainFrontend::BlockchainFrontend(BlockchainBackend& backend, BlockchainConfig config)
	: backend(backend), config(config), mevBuilder(backend), state(backend.newStateOverride()), running(true) {
	loadMempool();
	builderThread = std::thread(&BlockchainFrontend::blockBuilderLoop, this);
}

BlockchainFrontend::~BlockchainFrontend() {
	running = false;
	if (builderThread.joinable())
		builderThread.join();
	saveMempool();
}

void BlockchainFrontend::sendTransaction(const sp<Transaction>& transaction) {
	std::lock_guard _(mempoolMutex);
	transaction->apply(state.mut());
	mempool.add(transaction);
}

void BlockchainFrontend::rebuildSpeculativeState() {
	// Called under mempoolMutex.  After the backend commits a block, the
	// speculative state must be rebuilt from the new committed state and
	// the still-pending mempool transactions reapplied in order.
	state = backend.newStateOverride();
	for (const sp<Transaction>& tx : mempool) {
		tx->apply(state.mut());
	}
}

void BlockchainFrontend::blockBuilderLoop() {
	while (running) {
		usleep(10000);

		// TODO: Check if block building should be done
		//       Deadline computation is part of this
		uint64_t deadline = millis_since_epoch() + 10;

		ArrayList<sp<Transaction>> toBuild;
		{
			std::lock_guard _(mempoolMutex);
			if (mempool.size() > 0) {
				toBuild = mevBuilder.buildBlock(mempool, config.targetThroughput, deadline);
			}
		}

		if (toBuild.size() > 0) {
			long blockNumber = backend.addBlock(toBuild);
			if (blockNumber == -1) {
				// Block building was rejected (e.g. too early).  Put the
				// transactions back into the mempool in their original
				// positions.  We don't try to preserve absolute ordering
				// against newly-arrived mempool entries; the next pass
				// will reconsider everything.
				std::lock_guard _(mempoolMutex);
				mempool.addMany(toBuild);
			} else {
				// Committed: rebuild the speculative state so the frontend
				// view stops double-counting the committed transactions.
				std::lock_guard _(mempoolMutex);
				rebuildSpeculativeState();
			}
		}
	}
}

void BlockchainFrontend::saveMempool() {
	std::string path = backend.getDataDir() + "/mempool.bin";
	FdHandle fd = FdHandle::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0660);
	if (!fd) return;

	std::lock_guard _(mempoolMutex);
	uint32_t count = mempool.size();
	fd.write(count);

	size_t totalSize = 0;
	for (const sp<Transaction>& tx : mempool) totalSize += tx->size();

	if (totalSize > 0) {
		if (ftruncate(fd.getFd(), sizeof(uint32_t) + totalSize) != 0) return;

		MmapHandle mmap = fd.getMmapHandle(0, sizeof(uint32_t) + totalSize);
		if (mmap) {
			mmap.seek(sizeof(uint32_t));
			for (const sp<Transaction>& tx : mempool) {
				tx->write(&mmap);
			}
			msync(mmap.directPointer<void>(), sizeof(uint32_t) + totalSize, MS_SYNC);
		}
	}
	fsync(fd.getFd());
}

void BlockchainFrontend::loadMempool() {
	std::string path = backend.getDataDir() + "/mempool.bin";
	FdHandle fd = FdHandle::open(path.c_str(), O_RDONLY);
	if (!fd) return;

	uint32_t count;
	if (fd.read(count) != sizeof(uint32_t)) return;
	if (count == 0) return;

	off_t fileSize = fd.seek(0, SEEK_END);
	if (fileSize <= (off_t)sizeof(uint32_t)) return;

	MmapHandle mmap = fd.getMmapHandle(0, fileSize, PROT_READ);
	if (!mmap) return;

	mmap.seek(sizeof(uint32_t));
	std::lock_guard _(mempoolMutex);
	const TransactionTypeRegistry& reg = backend.getTransactionTypeRegistry();
	for (uint32_t i = 0; i < count; ++i) {
		sp<Transaction> tx = reg.read(&mmap);
		if (tx) {
			tx->apply(state.mut());
			mempool.add(tx);
		}
	}
}

sp<Transaction> BlockchainFrontend::getTransactionById(uint64_t id) const {
	uint64_t blockNumber = id >> 20;
	uint32_t txIndex = id & 0xFFFFF;

	ArrayList<sp<Transaction>> transactions = backend.getBlock(blockNumber);
	if (txIndex < (uint32_t)transactions.size()) {
		return transactions.get(txIndex);
	}

	return nullptr;
}

ArrayList<sp<Transaction>> BlockchainFrontend::getTransactionsByTimeWindow(uint64_t startMillis, uint64_t endMillis) {
	ArrayList<sp<Transaction>> results;

	// Check mempool
	if (backend.getLastBlockTimestamp() < endMillis) {
		std::lock_guard _(mempoolMutex);
		for (const sp<Transaction>& tx : mempool) {
			if (tx->getTimestamp() >= startMillis && tx->getTimestamp() <= endMillis) {
				results.add(tx);
			}
		}
	}

	results.addMany(backend.getTransactionsByTimeWindow(startMillis, endMillis));
	
	return results;
}
