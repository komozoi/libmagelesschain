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
#include "BlockchainBackend.h"
#include "universaltime.h"

MEVBuilder::MEVBuilder(BlockchainBackend& backend) : backend(backend) {}

ArrayList<sp<Transaction>> MEVBuilder::buildBlock(ArrayList<sp<Transaction>>& mempool, uint16_t maxTransactions, uint64_t deadline) const {
	if (mempool.size() == 0) return {};

	// Fresh override representing committed state.  Forks of this for
	// alternative candidate orderings would CoW per-family via sp<T>; the
	// current implementation only explores one greedy order so it uses a
	// single mutable override.
	sp<StateOverride> simState = backend.newStateOverride();

	ArrayList<sp<Transaction>> selected;
	ArrayList<int> indicesToRemove;

	while (selected.size() < maxTransactions && indicesToRemove.size() < mempool.size()) {
		sp<Transaction> bestTx;
		float bestValue = -1.0e30f;
		int bestIdx = -1;

		for (int i = 0; i < mempool.size(); ++i) {
			// Skip transactions already selected (we don't remove from mempool yet to keep indices stable)
			bool alreadySelected = false;
			for (int idx : indicesToRemove) if (idx == i) { alreadySelected = true; break; }
			if (alreadySelected) continue;

			sp<Transaction> tx = mempool.get(i);
			float val = tx->computeValue(*simState);
			if (val > bestValue) {
				bestValue = val;
				bestTx = tx;
				bestIdx = i;
			}
		}

		if (bestTx && bestValue >= 0) {
			bestTx->apply(simState.mut());
			selected.add(bestTx);
			indicesToRemove.add(bestIdx);
		} else {
			break;
		}

		if (deadline != 0 && millis_since_epoch() >= deadline) break;
	}

	// Optimization loop: try to improve the selected ordering until the
	// deadline.  Placeholder; future versions can explore swaps/2-opt etc.
	while (deadline != 0 && millis_since_epoch() < deadline) {
		// TODO: explore swap and reordering improvements.
		break;
	}

	// Remove selected transactions from the mempool while preserving the
	// relative order of the rest.  Selection-sort the indices descending so
	// removing earlier targets doesn't shift the indices of later targets.
	int n = indicesToRemove.size();
	for (int i = 0; i < n - 1; ++i) {
		int maxPos = i;
		for (int j = i + 1; j < n; ++j) {
			if (indicesToRemove.get(j) > indicesToRemove.get(maxPos)) {
				maxPos = j;
			}
		}
		if (maxPos != i) {
			int tmp = indicesToRemove.get(i);
			indicesToRemove.set(i, indicesToRemove.get(maxPos));
			indicesToRemove.set(maxPos, tmp);
		}
	}
	for (int idx : indicesToRemove) {
		// Ordered remove
		for (int i = idx; i < mempool.size() - 1; ++i) {
			mempool.set(i, std::move(mempool.get(i + 1)));
		}
		mempool.pop();
	}

	return selected;
}
