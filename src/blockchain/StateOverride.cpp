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

#include "StateOverride.h"

StateOverride::StateOverride(const StateOverrideRegistry& registry) : families(16) {
	const ArrayList<StateOverrideRegistry::Entry>& entries = registry.getEntries();
	for (int i = 0; i < entries.size(); ++i) {
		const StateOverrideRegistry::Entry& e = entries.get(i);
		ArrayList<sp<IndexOverrideFamilyBase>>* family = families.getPtr(e.key);
		if (!family) {
			families.put(e.key, ArrayList<sp<IndexOverrideFamilyBase>>());
			family = families.getPtr(e.key);
		}
		family->add(e.factory());
		Slot slot;
		slot.key = e.key;
		slot.id = e.id;
		order.add(slot);
	}
}

StateOverride::StateOverride(const StateOverride& other) : families(16), order(other.order) {
	// Shallow-copy the sp<IndexOverrideFamilyBase> handles.  Per libexcessive
	// sp<T> semantics, the copied sp's enter COPY_ON_WRITE state, so a later
	// .mut() on any one family deep-copies only that family.  This is the
	// CoW that makes MEVBuilder candidate forks cheap.
	for (int i = 0; i < other.order.size(); ++i) {
		const Slot& s = other.order.get(i);
		const ArrayList<sp<IndexOverrideFamilyBase>>* otherFamily = other.families.getPtr(s.key);
		ArrayList<sp<IndexOverrideFamilyBase>>* family = families.getPtr(s.key);
		if (!family) {
			families.put(s.key, ArrayList<sp<IndexOverrideFamilyBase>>());
			family = families.getPtr(s.key);
		}
		// Only add the entry for this slot once.  Because order may list the
		// same TypeKey multiple times (one per id), we only copy from the
		// other family up to our current size.
		if ((int)s.id >= family->size()) {
			family->add(otherFamily->get(s.id));
		}
	}
}

StateOverride& StateOverride::operator=(const StateOverride& other) {
	if (this == &other) return *this;
	families = HashMap<TypeKey, ArrayList<sp<IndexOverrideFamilyBase>>>(16);
	order = ArrayList<Slot>();
	for (int i = 0; i < other.order.size(); ++i) {
		const Slot& s = other.order.get(i);
		const ArrayList<sp<IndexOverrideFamilyBase>>* otherFamily = other.families.getPtr(s.key);
		ArrayList<sp<IndexOverrideFamilyBase>>* family = families.getPtr(s.key);
		if (!family) {
			families.put(s.key, ArrayList<sp<IndexOverrideFamilyBase>>());
			family = families.getPtr(s.key);
		}
		if ((int)s.id >= family->size()) {
			family->add(otherFamily->get(s.id));
		}
		order.add(s);
	}
	return *this;
}

void StateOverride::forEachFamily(void (*fn)(TypeKey, uint8_t, IndexOverrideFamilyBase&, void*), void* ctx) {
	for (int i = 0; i < order.size(); ++i) {
		const Slot& s = order.get(i);
		ArrayList<sp<IndexOverrideFamilyBase>>* family = families.getPtr(s.key);
		fn(s.key, s.id, family->get(s.id).mut(), ctx);
	}
}
