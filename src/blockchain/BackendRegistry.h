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

#ifndef LIBMAGELESSCHAIN_BACKENDREGISTRY_H
#define LIBMAGELESSCHAIN_BACKENDREGISTRY_H

#include "TypeKey.h"
#include "BlockchainIndex.h"
#include "IndexFamily.h"

#include "alloc/pointer.h"
#include "ds/ArrayList.h"
#include "ds/HashMap.h"

/*
 * Per-backend typed registry for Index instances.
 *
 * The application's ChainDesign::registerIndexes(BackendRegistry&) populates
 * this during backend construction.  Once construction finishes the
 * registry is locked: instances are accessed via backend.index<T>(id) which
 * delegates here.
 *
 * Type erasure is confined to the family boundary.  Instances are stored
 * inside IndexFamily<T> as sp<T>, so when application code retrieves an
 * instance it gets back a strongly-typed sp<T> with no casts.
 */
class BackendRegistry {
public:
	/*
	 * Ordered registration record.  Each call to registerIndex() appends
	 * one Entry.  The index of the entry inside `entries` is the
	 * `persistentTypeId` recorded in the on-disk catalog: it survives across
	 * restarts as long as the application's ChainDesign registers indexes
	 * in the same order (the same contract that already backs
	 * backend.index<T>(id)).
	 */
	struct Entry {
		TypeKey key;
		uint8_t id;
	};

	BackendRegistry() : families(16) {}

	template<typename T>
	void registerIndex(sp<T> instance) {
		TypeKey key = typeKey<T>();
		uint8_t newId;
		sp<IndexFamilyBase>* existingPtr = families.getPtr(key);
		if (!existingPtr) {
			sp<IndexFamily<T>> fam = sp<IndexFamily<T>>::create();
			fam.mut().add(std::move(instance));
			families.put(key, sp<IndexFamilyBase>(std::move(fam)));
			newId = 0;
		} else {
			IndexFamily<T>& fam = (IndexFamily<T>&)existingPtr->mut();
			newId = (uint8_t)fam.instanceCount();
			fam.add(std::move(instance));
		}
		Entry e;
		e.key = key;
		e.id = newId;
		entries.add(e);
	}

	template<typename T>
	sp<T> getIndex(uint8_t id) const {
		TypeKey key = typeKey<T>();
		const sp<IndexFamilyBase>* famPtr = families.getPtr(key);
		if (!famPtr) return sp<T>();
		const IndexFamily<T>& fam = (const IndexFamily<T>&)**famPtr;
		return fam.get(id);
	}

	template<typename T>
	int instanceCount() const {
		TypeKey key = typeKey<T>();
		const sp<IndexFamilyBase>* famPtr = families.getPtr(key);
		if (!famPtr) return 0;
		return (*famPtr)->instanceCount();
	}

	/*
	 * Type-erased accessor used by the backend's commit and load paths to
	 * walk all registered indexes in registration order.  Application code
	 * should not call this; use the typed backend.index<T>(id) instead.
	 */
	sp<BlockchainIndex> getIndexAt(uint16_t persistentTypeId) const {
		if ((int)persistentTypeId >= entries.size()) return sp<BlockchainIndex>();
		const Entry& e = entries.get(persistentTypeId);
		const sp<IndexFamilyBase>* famPtr = families.getPtr(e.key);
		if (!famPtr) return sp<BlockchainIndex>();
		return (*famPtr)->getInstance(e.id);
	}

	const ArrayList<Entry>& getEntries() const { return entries; }

private:
	HashMap<TypeKey, sp<IndexFamilyBase>> families;
	// Preserves registration order; index here == persistentTypeId.
	ArrayList<Entry> entries;
};

#endif //LIBMAGELESSCHAIN_BACKENDREGISTRY_H
