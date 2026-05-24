/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-24
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

#ifndef LIBMAGELESSCHAIN_STATEOVERRIDE_H
#define LIBMAGELESSCHAIN_STATEOVERRIDE_H

#include "TypeKey.h"
#include "IndexOverride.h"
#include "StateOverrideRegistry.h"

#include "alloc/pointer.h"
#include "ds/ArrayList.h"
#include "ds/HashMap.h"

/*
 * Concrete, library-owned container for one block-in-progress worth of
 * pending state modifications.
 *
 * Replaces the old cumulative-state BlockchainStateSnapshot.  Instead of
 * tracking the entire chain state in RAM, a StateOverride only carries the
 * deltas that pending transactions have produced on top of the committed
 * chain.  Untouched parts of the chain are read through the committed
 * indexes directly.
 *
 * Typed access:
 *   s.override<MyOverrideFamily>(id) returns a MyOverrideFamily& with no
 *   casts at the application level.  Internally the family lives behind a
 *   sp<IndexOverrideFamilyBase> stored in a TypeKey-keyed table, so the cast
 *   from base to derived happens once, in the library, at the typed
 *   accessor's boundary.
 *
 * Copy-on-write:
 *   sp<StateOverride> uses libexcessive's built-in CoW.  When MEVBuilder
 *   forks a candidate, it just sp-copies the override; mutating one family
 *   via `.mut()` deep-copies only that family.  Override families therefore
 *   must be copy-constructible.
 */
class StateOverride {
public:
	/*
	 * Build a fresh, empty StateOverride from the given registry.  One
	 * empty IndexOverrideFamilyBase instance is created per registered
	 * (type, id) entry.
	 */
	explicit StateOverride(const StateOverrideRegistry& registry);

	StateOverride(const StateOverride& other);
	StateOverride& operator=(const StateOverride& other);

	/*
	 * Typed access to a registered override family instance.  The caller
	 * gets back a reference to the concrete subclass and may freely read
	 * and write through it.
	 */
	template<typename T>
	T& override(uint8_t id) {
		TypeKey key = typeKey<T>();
		ArrayList<sp<IndexOverrideFamilyBase>>* family = families.getPtr(key);
		// id and registration must have been set up by ChainDesign; if not,
		// this is a programming error in the application.
		return (T&)family->get(id).mut();
	}

	template<typename T>
	const T& override(uint8_t id) const {
		TypeKey key = typeKey<T>();
		const ArrayList<sp<IndexOverrideFamilyBase>>* family = families.getPtr(key);
		return (const T&)*family->get(id);
	}

	/*
	 * Iterate all registered (type, id) entries and call seal() on each.
	 * Used by the backend at commit time and reserved for the future
	 * segment-storage layer.
	 */
	void forEachFamily(void (*fn)(TypeKey, uint8_t, IndexOverrideFamilyBase&, void*), void* ctx);

private:
	HashMap<TypeKey, ArrayList<sp<IndexOverrideFamilyBase>>> families;
	// Preserves registration order so forEachFamily walks families in a
	// deterministic order matching ChainDesign::registerOverrides.
	struct Slot {
		TypeKey key;
		uint8_t id;
	};
	ArrayList<Slot> order;
};

#endif //LIBMAGELESSCHAIN_STATEOVERRIDE_H
