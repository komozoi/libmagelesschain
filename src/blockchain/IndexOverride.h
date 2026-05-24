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

#ifndef LIBMAGELESSCHAIN_INDEXOVERRIDE_H
#define LIBMAGELESSCHAIN_INDEXOVERRIDE_H

#include <cstdint>

#include "ds/Bytestring.h"

class Index;

/*
 * Base for an application-defined family of index overrides.
 *
 * An override family is the layer through which transactions read and write
 * pending modifications to a particular kind of index, on top of the
 * committed chain state.  An instance corresponds one-to-one with an
 * IndexFamily<T>::instance(id) entry on the backend side.
 *
 * Each concrete override family is a sibling of a concrete Index type and
 * knows how to:
 *   - read through to the underlying Index for committed values,
 *   - layer in any pending changes made by transactions in the current
 *     block-in-progress (or the speculative mempool view in the frontend),
 *   - produce a segment payload at seal-time, which is what the backend
 *     writes through the matching BlockchainIndex when the block commits.
 *
 * Copy semantics:
 *   Concrete override families must be copy-constructible so that sp<T>'s
 *   built-in copy-on-write does the right thing when a MEVBuilder candidate
 *   is forked from the current override.  Untouched families are shared;
 *   only the families a candidate actually writes to get deep-copied.
 */
class IndexOverrideFamilyBase {
public:
	virtual ~IndexOverrideFamilyBase() = default;

	/*
	 * Encode the override's pending changes as a segment payload.  The
	 * backend will hand this to the matching BlockchainIndex at commit time.
	 * Returning an empty Bytestring signals "no changes for this index in
	 * this block" and the catalog entry is skipped.
	 *
	 * In Phase 1 this is allowed to return an empty Bytestring as a
	 * placeholder while segment storage is still being wired up.
	 */
	virtual Bytestring seal() const { return Bytestring(); }
};

#endif //LIBMAGELESSCHAIN_INDEXOVERRIDE_H
