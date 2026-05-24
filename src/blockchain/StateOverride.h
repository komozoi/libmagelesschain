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
 * A StateOverride carries the deltas that pending transactions have
 * produced on top of the committed chain.  Untouched parts of the chain
 * are read through the committed indexes directly, keeping the in-RAM
 * footprint bounded by the size of the pending block rather than the
 * size of the chain.
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
		// The (type, id) pair must have been registered through ChainDesign;
		// reaching here with an unregistered family is an application bug.
		return (T&)family->get(id).mut();
	}

	template<typename T>
	const T& override(uint8_t id) const {
		TypeKey key = typeKey<T>();
		const ArrayList<sp<IndexOverrideFamilyBase>>* family = families.getPtr(key);
		return (const T&)*family->get(id);
	}

	/*
	 * Iterate every registered (type, id) family in registration order,
	 * invoking `fn` with the family's TypeKey, instance id, mutable
	 * reference, and the caller's context pointer.  Used by tests and by
	 * tooling that wants to walk every family without knowing its concrete
	 * type up front.
	 */
	void forEachFamily(void (*fn)(TypeKey, uint8_t, IndexOverrideFamilyBase&, void*), void* ctx);

	/*
	 * Number of registered override family slots, matching
	 * StateOverrideRegistry::getEntries().size().  The slot at index N is
	 * the same registration-order entry as registry.getEntries().get(N).
	 */
	int familySize() const { return order.size(); }

	/*
	 * Mutable access to the override family at registration-order slot N.
	 * Used by the backend commit path to seal each family and pair it
	 * one-to-one with the matching index.
	 */
	IndexOverrideFamilyBase& familyAt(int n);
	const IndexOverrideFamilyBase& familyAt(int n) const;

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
