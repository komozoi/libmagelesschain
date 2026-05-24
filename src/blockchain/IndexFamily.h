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

#ifndef LIBMAGELESSCHAIN_INDEXFAMILY_H
#define LIBMAGELESSCHAIN_INDEXFAMILY_H

#include <cstdint>

#include "alloc/pointer.h"
#include "ds/ArrayList.h"

/*
 * Erased family base.  Only used as the storage type inside BackendRegistry's
 * type-keyed table.  Application code never sees this; it always interacts
 * with the typed IndexFamily<T> through backend.index<T>(id).
 */
class IndexFamilyBase {
public:
	virtual ~IndexFamilyBase() = default;
	virtual int instanceCount() const = 0;
};

/*
 * Typed container holding all registered instances of a particular Index
 * subclass.  Instances are stored in an ArrayList and addressed by uint8_t
 * id corresponding to their registration order within the family.
 *
 * IDs are dense (0..N-1).  Out-of-range ids in get() return a null sp<T>.
 */
template<typename T>
class IndexFamily : public IndexFamilyBase {
public:
	void add(sp<T> instance) {
		instances.add(std::move(instance));
	}

	sp<T> get(uint8_t id) const {
		if ((int)id >= instances.size()) return sp<T>();
		return instances.get(id);
	}

	int instanceCount() const override {
		return instances.size();
	}

private:
	ArrayList<sp<T>> instances;
};

#endif //LIBMAGELESSCHAIN_INDEXFAMILY_H
