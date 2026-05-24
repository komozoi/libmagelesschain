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

#ifndef LIBMAGELESSCHAIN_TYPEKEY_H
#define LIBMAGELESSCHAIN_TYPEKEY_H

#include <cstdint>

/*
 * A lightweight type identifier.  Each distinct T resolves to a stable, process-unique address
 * that can be used as a key in a HashMap.  No std::type_index, no RTTI dependency, and no
 * static initializer ordering hazards: the address of a function-local static is computed lazily
 * on first call and never changes thereafter.
 */
typedef const void* TypeKey;

template<typename T>
TypeKey typeKey() {
	static const char tag = 0;
	return (const void*)&tag;
}

#endif //LIBMAGELESSCHAIN_TYPEKEY_H
