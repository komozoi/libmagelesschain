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
	BackendRegistry() : families(16) {}

	template<typename T>
	void registerIndex(sp<T> instance) {
		TypeKey key = typeKey<T>();
		sp<IndexFamilyBase>* existingPtr = families.getPtr(key);
		if (!existingPtr) {
			sp<IndexFamily<T>> fam = sp<IndexFamily<T>>::create();
			fam.mut().add(std::move(instance));
			families.put(key, sp<IndexFamilyBase>(std::move(fam)));
		} else {
			IndexFamily<T>& fam = (IndexFamily<T>&)existingPtr->mut();
			fam.add(std::move(instance));
		}
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

private:
	HashMap<TypeKey, sp<IndexFamilyBase>> families;
};

#endif //LIBMAGELESSCHAIN_BACKENDREGISTRY_H
