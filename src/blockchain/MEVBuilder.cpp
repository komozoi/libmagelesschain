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

#include "MEVBuilder.h"
#include <algorithm>
#include "universaltime.h"
#include "BlockchainBackend.h"

MEVBuilder::MEVBuilder(BlockchainBackend& backend)
	: backend(backend) {}

ArrayList<sp<Transaction>> MEVBuilder::buildBlock(ArrayList<sp<Transaction>>& mempool, uint16_t maxTransactions, uint64_t deadline) const {
	if (mempool.size() == 0) return {};

	sp<BlockchainStateSnapshot> state = backend.getLatestState();

	ArrayList<sp<Transaction>> selected;
	ArrayList<int> indicesToRemove;

	while (selected.size() < maxTransactions && mempool.size() > 0) {
		sp<Transaction> bestTx;
		float bestValue = -1.0e30f; // Very small
		int bestIdx = -1;

		for (int i = 0; i < mempool.size(); ++i) {
			// Skip transactions already selected (we don't remove from mempool yet to keep indices stable)
			bool alreadySelected = false;
			for (int idx : indicesToRemove) if (idx == i) { alreadySelected = true; break; }
			if (alreadySelected) continue;

			sp<Transaction> tx = mempool.get(i);
			float val = tx->computeValue(state.mut());
			if (val > bestValue) {
				bestValue = val;
				bestTx = tx;
				bestIdx = i;
			}
		}

		if (bestTx && bestValue >= 0) {
			bestTx->apply(state.mut());
			selected.add(bestTx);
			indicesToRemove.add(bestIdx);
		} else {
			break;
		}

		if (millis_since_epoch() >= deadline && deadline != 0) break;
	}

	// While time remains and further optimization is possible, try to reorder or change transactions to improve the total value.
	while (millis_since_epoch() < deadline) {
		// TODO: Implement
		break;
	}

	// Remove selected transactions from the mempool.
	// Sort indices in descending order to remove from back to front
	std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<int>());
	for (int idx : indicesToRemove) {
		// Ordered remove
		for (int i = idx; i < mempool.size() - 1; ++i) {
			mempool.set(i, std::move(mempool.get(i + 1)));
		}
		mempool.pop();
	}

	return selected;
}
