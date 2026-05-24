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

#include <gtest/gtest.h>

#include "blockchain/TypeKey.h"

/*
 * Tiny tests pinning the contract of typeKey<T>():
 *  - stable across calls within a TU,
 *  - distinct for distinct T,
 *  - stable across translation units for the same T.
 *
 * The cross-TU witness lives in test_TypeKey_helper.cpp.
 */

struct TypeKeyProbeA { int x; };
struct TypeKeyProbeB { int x; };

struct TypeKeyCrossTuProbe { int x; };
TypeKey typeKeyFromOtherTu_TypeKeyCrossTuProbe();

TEST(TypeKeyTest, StableWithinTU) {
	EXPECT_EQ(typeKey<TypeKeyProbeA>(), typeKey<TypeKeyProbeA>());
}

TEST(TypeKeyTest, DistinctTypesDifferentKeys) {
	EXPECT_NE(typeKey<TypeKeyProbeA>(), typeKey<TypeKeyProbeB>());
}

TEST(TypeKeyTest, StableAcrossTranslationUnits) {
	EXPECT_EQ(typeKey<TypeKeyCrossTuProbe>(), typeKeyFromOtherTu_TypeKeyCrossTuProbe());
}

TEST(TypeKeyTest, NonNull) {
	EXPECT_NE(typeKey<TypeKeyProbeA>(), (TypeKey)nullptr);
}
