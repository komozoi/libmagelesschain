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

#ifndef LIBMAGELESSCHAIN_STATEOVERRIDEREGISTRY_H
#define LIBMAGELESSCHAIN_STATEOVERRIDEREGISTRY_H

#include "TypeKey.h"
#include "IndexOverride.h"

#include "alloc/pointer.h"
#include "ds/ArrayList.h"
#include "ds/HashMap.h"

/*
 * Per-backend registry of override-family factories.
 *
 * The application's ChainDesign::registerOverrides(StateOverrideRegistry&)
 * declares, in registration order, which IndexOverrideFamilyBase subclasses
 * exist and how to create a fresh empty instance for each.
 *
 * StateOverride uses this at construction time to materialize one empty
 * instance per registered (type, id) entry, so that transactions can call
 * s.override<T>(id) without further setup.
 */
class StateOverrideRegistry {
public:
	typedef sp<IndexOverrideFamilyBase> (*FactoryFunc)();

	struct Entry {
		TypeKey key;
		uint8_t id;
		FactoryFunc factory;
	};

	StateOverrideRegistry() : counts(8) {}

	/*
	 * Register one instance of override family T.  Multiple calls with the
	 * same T allocate successive ids (0, 1, 2, ...).
	 */
	template<typename T>
	void registerOverride() {
		TypeKey key = typeKey<T>();
		int* cnt = counts.getPtr(key);
		uint8_t id = cnt ? (uint8_t)*cnt : (uint8_t)0;
		if (cnt) (*cnt)++;
		else counts.put(key, 1);

		Entry e;
		e.key = key;
		e.id = id;
		e.factory = &factoryImpl<T>;
		entries.add(e);
	}

	const ArrayList<Entry>& getEntries() const { return entries; }

private:
	template<typename T>
	static sp<IndexOverrideFamilyBase> factoryImpl() {
		// Build as UNIQUE so subsequent copies become COW.  Without this,
		// sp<T>::create() would return SHARED — and SHARED sps never
		// detach on mut(), aliasing override-family data across the
		// committed state, the frontend's speculative state, and every
		// MEV candidate.
		sp<T> instance(UNIQUE);
		return sp<IndexOverrideFamilyBase>(std::move(instance));
	}

	ArrayList<Entry> entries;
	HashMap<TypeKey, int> counts;
};

#endif //LIBMAGELESSCHAIN_STATEOVERRIDEREGISTRY_H
