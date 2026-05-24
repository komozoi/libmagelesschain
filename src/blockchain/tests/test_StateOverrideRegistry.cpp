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

#include "blockchain/StateOverrideRegistry.h"
#include "blockchain/StateOverride.h"
#include "blockchain/IndexOverride.h"

/*
 * Pins the contract for the override-family registry: registration order
 * is preserved, multiple instances of the same family get successive ids,
 * and a StateOverride built from the registry exposes exactly the families
 * that were registered.
 */

class StateOverrideRegistryProbeA : public IndexOverrideFamilyBase {
public:
	int valueA = 0;
};

class StateOverrideRegistryProbeB : public IndexOverrideFamilyBase {
public:
	float valueB = 0.0f;
};

TEST(StateOverrideRegistryTest, RegistrationOrderIsPreserved) {
	StateOverrideRegistry reg;
	reg.registerOverride<StateOverrideRegistryProbeA>();
	reg.registerOverride<StateOverrideRegistryProbeB>();
	reg.registerOverride<StateOverrideRegistryProbeA>();

	const ArrayList<StateOverrideRegistry::Entry>& entries = reg.getEntries();
	ASSERT_EQ(entries.size(), 3);
	EXPECT_EQ(entries.get(0).key, typeKey<StateOverrideRegistryProbeA>());
	EXPECT_EQ(entries.get(0).id, 0);
	EXPECT_EQ(entries.get(1).key, typeKey<StateOverrideRegistryProbeB>());
	EXPECT_EQ(entries.get(1).id, 0);
	EXPECT_EQ(entries.get(2).key, typeKey<StateOverrideRegistryProbeA>());
	EXPECT_EQ(entries.get(2).id, 1);
}

TEST(StateOverrideRegistryTest, MultipleSameTypeSuccessiveIds) {
	StateOverrideRegistry reg;
	for (int i = 0; i < 4; ++i) {
		reg.registerOverride<StateOverrideRegistryProbeA>();
	}
	const ArrayList<StateOverrideRegistry::Entry>& entries = reg.getEntries();
	ASSERT_EQ(entries.size(), 4);
	for (int i = 0; i < 4; ++i) {
		EXPECT_EQ(entries.get(i).id, (uint8_t)i);
	}
}

TEST(StateOverrideRegistryTest, StateOverrideMaterializesRegisteredFamilies) {
	StateOverrideRegistry reg;
	reg.registerOverride<StateOverrideRegistryProbeA>();
	reg.registerOverride<StateOverrideRegistryProbeB>();

	StateOverride state(reg);
	// Mutate and read back through the typed accessor.
	state.override<StateOverrideRegistryProbeA>(0).valueA = 42;
	state.override<StateOverrideRegistryProbeB>(0).valueB = 3.14f;

	EXPECT_EQ(state.override<StateOverrideRegistryProbeA>(0).valueA, 42);
	EXPECT_FLOAT_EQ(state.override<StateOverrideRegistryProbeB>(0).valueB, 3.14f);
}

TEST(StateOverrideRegistryTest, StateOverrideRepeatedAccessReturnsSameInstance) {
	StateOverrideRegistry reg;
	reg.registerOverride<StateOverrideRegistryProbeA>();

	StateOverride state(reg);
	StateOverrideRegistryProbeA& first = state.override<StateOverrideRegistryProbeA>(0);
	first.valueA = 99;
	StateOverrideRegistryProbeA& second = state.override<StateOverrideRegistryProbeA>(0);
	EXPECT_EQ(&first, &second);
	EXPECT_EQ(second.valueA, 99);
}

TEST(StateOverrideRegistryTest, MultipleIdsAreIndependent) {
	StateOverrideRegistry reg;
	reg.registerOverride<StateOverrideRegistryProbeA>();
	reg.registerOverride<StateOverrideRegistryProbeA>();

	StateOverride state(reg);
	state.override<StateOverrideRegistryProbeA>(0).valueA = 1;
	state.override<StateOverrideRegistryProbeA>(1).valueA = 2;
	EXPECT_EQ(state.override<StateOverrideRegistryProbeA>(0).valueA, 1);
	EXPECT_EQ(state.override<StateOverrideRegistryProbeA>(1).valueA, 2);
}

TEST(StateOverrideRegistryTest, EmptyStateOverride) {
	StateOverrideRegistry reg;
	StateOverride state(reg);
	// No assertions on families since none were registered.  Just verify
	// construction succeeds and forEachFamily completes (touches zero
	// entries).
	int callCount = 0;
	state.forEachFamily(
		[](TypeKey, uint8_t, IndexOverrideFamilyBase&, void* ctx) {
			(*(int*)ctx)++;
		},
		&callCount);
	EXPECT_EQ(callCount, 0);
}

TEST(StateOverrideRegistryTest, ForEachFamilyWalksInRegistrationOrder) {
	StateOverrideRegistry reg;
	reg.registerOverride<StateOverrideRegistryProbeA>();
	reg.registerOverride<StateOverrideRegistryProbeB>();
	reg.registerOverride<StateOverrideRegistryProbeA>();

	StateOverride state(reg);

	struct Trace {
		TypeKey keys[8];
		uint8_t ids[8];
		int count = 0;
	};
	Trace t;
	state.forEachFamily(
		[](TypeKey key, uint8_t id, IndexOverrideFamilyBase&, void* ctx) {
			Trace* tr = (Trace*)ctx;
			tr->keys[tr->count] = key;
			tr->ids[tr->count] = id;
			tr->count++;
		},
		&t);
	ASSERT_EQ(t.count, 3);
	EXPECT_EQ(t.keys[0], typeKey<StateOverrideRegistryProbeA>());
	EXPECT_EQ(t.ids[0], 0);
	EXPECT_EQ(t.keys[1], typeKey<StateOverrideRegistryProbeB>());
	EXPECT_EQ(t.ids[1], 0);
	EXPECT_EQ(t.keys[2], typeKey<StateOverrideRegistryProbeA>());
	EXPECT_EQ(t.ids[2], 1);
}
