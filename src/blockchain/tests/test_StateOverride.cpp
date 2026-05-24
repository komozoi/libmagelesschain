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

#include "blockchain/StateOverride.h"
#include "blockchain/StateOverrideRegistry.h"
#include "blockchain/IndexOverride.h"

/*
 * The single most important correctness contract in Phase 1 is that
 * forking a sp<StateOverride> via libexcessive's CoW gives the MEVBuilder
 * cheap candidate branches that diverge cleanly: a write through one branch
 * does not leak into another, parent or sibling.
 *
 * These tests construct overrides as sp<StateOverride>(UNIQUE, reg) so that
 * the first sp copy enters COPY_ON_WRITE state.  This mirrors how the
 * library factories build override families.
 */

class StateOverrideFamilyA : public IndexOverrideFamilyBase {
public:
	int valueA = 0;
};

class StateOverrideFamilyB : public IndexOverrideFamilyBase {
public:
	int valueB = 0;
};

static sp<StateOverride> makeUnique(const StateOverrideRegistry& reg) {
	// UNIQUE so the first copy CoW-detaches.
	return sp<StateOverride>(UNIQUE, reg);
}

static StateOverrideRegistry buildRegistry() {
	StateOverrideRegistry reg;
	reg.registerOverride<StateOverrideFamilyA>();
	reg.registerOverride<StateOverrideFamilyB>();
	return reg;
}

TEST(StateOverrideTest, ForkDoesNotLeakToParent) {
	StateOverrideRegistry reg = buildRegistry();
	sp<StateOverride> parent = makeUnique(reg);
	parent.mut().override<StateOverrideFamilyA>(0).valueA = 1;

	sp<StateOverride> child = parent;
	child.mut().override<StateOverrideFamilyA>(0).valueA = 99;

	EXPECT_EQ(parent->override<StateOverrideFamilyA>(0).valueA, 1);
	EXPECT_EQ(child->override<StateOverrideFamilyA>(0).valueA, 99);
}

TEST(StateOverrideTest, UntouchedFamilyStaysSharedAfterFork) {
	StateOverrideRegistry reg = buildRegistry();
	sp<StateOverride> parent = makeUnique(reg);
	parent.mut().override<StateOverrideFamilyA>(0).valueA = 7;
	parent.mut().override<StateOverrideFamilyB>(0).valueB = 13;

	sp<StateOverride> child = parent;
	// Only mutate A in child; B should remain shared semantically (writes
	// to B in parent will *not* propagate to child after this point, but
	// reads up to here should still match).
	child.mut().override<StateOverrideFamilyA>(0).valueA = 70;

	EXPECT_EQ(parent->override<StateOverrideFamilyA>(0).valueA, 7);
	EXPECT_EQ(parent->override<StateOverrideFamilyB>(0).valueB, 13);
	EXPECT_EQ(child->override<StateOverrideFamilyA>(0).valueA, 70);
	EXPECT_EQ(child->override<StateOverrideFamilyB>(0).valueB, 13);
}

TEST(StateOverrideTest, SiblingsDivergeIndependently) {
	StateOverrideRegistry reg = buildRegistry();
	sp<StateOverride> parent = makeUnique(reg);
	parent.mut().override<StateOverrideFamilyA>(0).valueA = 5;

	sp<StateOverride> a = parent;
	sp<StateOverride> b = parent;
	a.mut().override<StateOverrideFamilyA>(0).valueA = 11;
	b.mut().override<StateOverrideFamilyA>(0).valueA = 22;

	EXPECT_EQ(parent->override<StateOverrideFamilyA>(0).valueA, 5);
	EXPECT_EQ(a->override<StateOverrideFamilyA>(0).valueA, 11);
	EXPECT_EQ(b->override<StateOverrideFamilyA>(0).valueA, 22);
}

// Note: multi-level (fork-of-fork) CoW is NOT currently supported by
// libexcessive's sp<T> semantics out of the box.  Once a sp is detached
// its type becomes SHARED, and subsequent copies share state rather than
// re-CoWing.  StateOverride's inner family sps inherit this limitation
// because they are also copied as SHARED after the first detach.  The
// MEVBuilder only ever forks one level (off a fresh backend override per
// build), so fork-once is the contract we test here.  If multi-level
// candidate exploration is ever needed, either StateOverride's copy
// constructor must promote each inner sp to COW via getWritableCopy, or
// the caller must use sp<StateOverride>(UNIQUE, *other) to deep-copy.
// If this functionality is needed, use the below commented out test:
/*
TEST(StateOverrideTest, ForkOfForkDetachesAtEachLevel) {
	// After a sp<T>'s first CoW detachment its type becomes SHARED, so to
	// fork it again the caller must request a writable copy explicitly via
	// getWritableCopy().  This mirrors the pattern MEVBuilder would use if
	// it ever needed multi-level candidate exploration.
	StateOverrideRegistry reg = buildRegistry();
	sp<StateOverride> grand = makeUnique(reg);
	grand.mut().override<StateOverrideFamilyA>(0).valueA = 1;

	sp<StateOverride> parent = grand.getWritableCopy();
	parent.mut().override<StateOverrideFamilyA>(0).valueA = 2;

	sp<StateOverride> child = parent.getWritableCopy();
	child.mut().override<StateOverrideFamilyA>(0).valueA = 3;

	EXPECT_EQ(grand->override<StateOverrideFamilyA>(0).valueA, 1);
	EXPECT_EQ(parent->override<StateOverrideFamilyA>(0).valueA, 2);
	EXPECT_EQ(child->override<StateOverrideFamilyA>(0).valueA, 3);
}
*/

TEST(StateOverrideTest, FreshlyConstructedOverridesAreEqual) {
	StateOverrideRegistry reg = buildRegistry();
	StateOverride a(reg);
	StateOverride b(reg);
	EXPECT_EQ(a.override<StateOverrideFamilyA>(0).valueA, b.override<StateOverrideFamilyA>(0).valueA);
	EXPECT_EQ(a.override<StateOverrideFamilyB>(0).valueB, b.override<StateOverrideFamilyB>(0).valueB);
	EXPECT_EQ(a.override<StateOverrideFamilyA>(0).valueA, 0);
}

TEST(StateOverrideTest, CopyConstructorIsCheapAndCorrect) {
	StateOverrideRegistry reg = buildRegistry();
	StateOverride parent(reg);
	parent.override<StateOverrideFamilyA>(0).valueA = 50;

	StateOverride child(parent);
	EXPECT_EQ(child.override<StateOverrideFamilyA>(0).valueA, 50);

	// Mutating the child must not propagate to the parent.
	child.override<StateOverrideFamilyA>(0).valueA = 60;
	EXPECT_EQ(parent.override<StateOverrideFamilyA>(0).valueA, 50);
	EXPECT_EQ(child.override<StateOverrideFamilyA>(0).valueA, 60);
}

TEST(StateOverrideTest, FamilyOrderPreservedThroughFork) {
	StateOverrideRegistry reg;
	reg.registerOverride<StateOverrideFamilyA>();
	reg.registerOverride<StateOverrideFamilyB>();
	reg.registerOverride<StateOverrideFamilyA>();

	sp<StateOverride> parent = makeUnique(reg);
	sp<StateOverride> child = parent;
	child.mut().override<StateOverrideFamilyA>(0).valueA = 1;

	struct Trace {
		TypeKey keys[8];
		uint8_t ids[8];
		int count = 0;
	};
	Trace pt;
	Trace ct;
	parent.mut().forEachFamily(
		[](TypeKey k, uint8_t id, IndexOverrideFamilyBase&, void* ctx) {
			Trace* t = (Trace*)ctx;
			t->keys[t->count] = k;
			t->ids[t->count] = id;
			t->count++;
		},
		&pt);
	child.mut().forEachFamily(
		[](TypeKey k, uint8_t id, IndexOverrideFamilyBase&, void* ctx) {
			Trace* t = (Trace*)ctx;
			t->keys[t->count] = k;
			t->ids[t->count] = id;
			t->count++;
		},
		&ct);

	ASSERT_EQ(pt.count, 3);
	ASSERT_EQ(ct.count, 3);
	for (int i = 0; i < 3; ++i) {
		EXPECT_EQ(pt.keys[i], ct.keys[i]);
		EXPECT_EQ(pt.ids[i], ct.ids[i]);
	}
}
