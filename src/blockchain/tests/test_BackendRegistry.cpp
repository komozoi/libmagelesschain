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

#include "blockchain/BackendRegistry.h"
#include "blockchain/BlockchainIndex.h"

/*
 * Direct unit tests for the typed-family R3 registry contract.  These pin
 * the behavior that BlockchainBackend::index<T>(id) delegates to, separate
 * from anything else that touches the backend.
 */

class BackendRegistryProbeA : public BlockchainIndex {
public:
	int payload = 0;
	uint16_t encodingVersion() const override { return 0; }
	Bytestring mergeSegments(const ArrayList<segment_coordinate_t>&) const override {
		return Bytestring();
	}
};

class BackendRegistryProbeB : public BlockchainIndex {
public:
	float payload = 0.0f;
	uint16_t encodingVersion() const override { return 0; }
	Bytestring mergeSegments(const ArrayList<segment_coordinate_t>&) const override {
		return Bytestring();
	}
};

TEST(BackendRegistryTest, RegisterAndRetrieveIdentity) {
	BackendRegistry reg;
	sp<BackendRegistryProbeA> inst = sp<BackendRegistryProbeA>::create();
	inst.mut().payload = 7;
	reg.registerIndex<BackendRegistryProbeA>(inst);

	sp<BackendRegistryProbeA> got = reg.getIndex<BackendRegistryProbeA>(0);
	ASSERT_NE(got.get(), nullptr);
	EXPECT_EQ(got->payload, 7);
	// Both shared pointers should refer to the same underlying object.
	EXPECT_EQ(&*got, &*inst);
}

TEST(BackendRegistryTest, MultipleInstancesSameTypeIndependent) {
	BackendRegistry reg;
	sp<BackendRegistryProbeA> a = sp<BackendRegistryProbeA>::create();
	a.mut().payload = 1;
	sp<BackendRegistryProbeA> b = sp<BackendRegistryProbeA>::create();
	b.mut().payload = 2;
	reg.registerIndex<BackendRegistryProbeA>(a);
	reg.registerIndex<BackendRegistryProbeA>(b);

	EXPECT_EQ(reg.instanceCount<BackendRegistryProbeA>(), 2);
	EXPECT_EQ(reg.getIndex<BackendRegistryProbeA>(0)->payload, 1);
	EXPECT_EQ(reg.getIndex<BackendRegistryProbeA>(1)->payload, 2);
}

TEST(BackendRegistryTest, DifferentTypesDoNotCollide) {
	BackendRegistry reg;
	sp<BackendRegistryProbeA> a = sp<BackendRegistryProbeA>::create();
	a.mut().payload = 11;
	sp<BackendRegistryProbeB> b = sp<BackendRegistryProbeB>::create();
	b.mut().payload = 22.5f;
	reg.registerIndex<BackendRegistryProbeA>(a);
	reg.registerIndex<BackendRegistryProbeB>(b);

	EXPECT_EQ(reg.instanceCount<BackendRegistryProbeA>(), 1);
	EXPECT_EQ(reg.instanceCount<BackendRegistryProbeB>(), 1);
	EXPECT_EQ(reg.getIndex<BackendRegistryProbeA>(0)->payload, 11);
	EXPECT_FLOAT_EQ(reg.getIndex<BackendRegistryProbeB>(0)->payload, 22.5f);
}

TEST(BackendRegistryTest, OutOfRangeIdReturnsNull) {
	BackendRegistry reg;
	reg.registerIndex<BackendRegistryProbeA>(sp<BackendRegistryProbeA>::create());

	sp<BackendRegistryProbeA> oor = reg.getIndex<BackendRegistryProbeA>(5);
	EXPECT_EQ(oor.get(), nullptr);
}

TEST(BackendRegistryTest, UnregisteredTypeReturnsNull) {
	BackendRegistry reg;
	// Register only A.
	reg.registerIndex<BackendRegistryProbeA>(sp<BackendRegistryProbeA>::create());

	sp<BackendRegistryProbeB> missing = reg.getIndex<BackendRegistryProbeB>(0);
	EXPECT_EQ(missing.get(), nullptr);
	EXPECT_EQ(reg.instanceCount<BackendRegistryProbeB>(), 0);
}

TEST(BackendRegistryTest, EmptyRegistry) {
	BackendRegistry reg;
	EXPECT_EQ(reg.instanceCount<BackendRegistryProbeA>(), 0);
	EXPECT_EQ(reg.getIndex<BackendRegistryProbeA>(0).get(), nullptr);
}

TEST(BackendRegistryTest, IdsAreDenseAndPreserveRegistrationOrder) {
	BackendRegistry reg;
	for (int i = 0; i < 5; ++i) {
		sp<BackendRegistryProbeA> inst = sp<BackendRegistryProbeA>::create();
		inst.mut().payload = i * 10;
		reg.registerIndex<BackendRegistryProbeA>(inst);
	}
	EXPECT_EQ(reg.instanceCount<BackendRegistryProbeA>(), 5);
	for (int i = 0; i < 5; ++i) {
		EXPECT_EQ(reg.getIndex<BackendRegistryProbeA>((uint8_t)i)->payload, i * 10);
	}
}
