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
#include "BlockchainStateSnapshot.h"
#include "universaltime.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

BlockchainFrontend::BlockchainFrontend(BlockchainBackend& backend, sp<BlockchainStateSnapshot> initialState, BlockchainConfig config)
	: backend(backend), config(config), state(initialState), running(true) {
	reapplyHistory();
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
	if (state) transaction->write(state.mut());
	mempool.add(transaction);
}

void BlockchainFrontend::reapplyHistory() {
	if (!state) return;
	long height = backend.getBlockHeight();
	for (long h = 1; h <= height; ++h) {
		ArrayList<sp<Transaction>> transactions = backend.getBlock(h);
		for (const sp<Transaction>& tx : transactions) {
			tx->write(state.mut());
		}
	}
}

void BlockchainFrontend::blockBuilderLoop() {
	while (running) {
		ArrayList<sp<Transaction>> toBuild;
		{
			std::lock_guard _(mempoolMutex);
			if (mempool.size() > 0) {
				for (const sp<Transaction>& tx : mempool) {
					toBuild.add(tx);
				}
			}
		}

		if (toBuild.size() > 0) {
			long blockNumber = backend.addBlock(toBuild);
			if (blockNumber != -1) {
				std::lock_guard _(mempoolMutex);
				int numToRemove = toBuild.size();
				for (int i = 0; i < numToRemove; ++i) {
					if (mempool.size() > 0) {
						for (int j = 0; j < mempool.size() - 1; ++j) {
							mempool.set(j, std::move(mempool.get(j+1)));
						}
						mempool.pop();
					}
				}
			}
		}

		usleep(10000);
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
	for (uint32_t i = 0; i < count; ++i) {
		sp<Transaction> tx = Transaction::read(&mmap);
		if (tx) {
			if (state) tx->write(state.mut());
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
