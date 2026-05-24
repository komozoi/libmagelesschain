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

#include "blockchain/TransactionTypeRegistry.h"
#include "blockchain/Transaction.h"

/*
 * Pins the contract that the transaction type registry is per-instance
 * (not a global static), stable across re-registration, and rejects
 * unknown ids with a null sp.
 */

class TtrProbeTx : public Transaction {
public:
	int val;
	explicit TtrProbeTx(int v = 0) : val(v) {}

	bool verify(const StateOverride&) const override { return true; }
	bool apply(StateOverride&) const override { return true; }
	float computeValue(const StateOverride&) const override { return (float)val; }
	uint8_t getTypeId() const override { return 0x77; }
	uint64_t getTimestamp() const override { return 0; }
	size_t size() const override { return sizeof(uint8_t) + sizeof(int); }
	void write(MmapHandle*) const override {}

	static sp<Transaction> createFromMmap(MmapHandle*) {
		// Tests below do not exercise the deserialization path directly,
		// they just verify factory wiring/lookup.
		sp<TtrProbeTx> tx = sp<TtrProbeTx>::create(42);
		return tx;
	}
	static sp<Transaction> createFromMmapAlt(MmapHandle*) {
		sp<TtrProbeTx> tx = sp<TtrProbeTx>::create(99);
		return tx;
	}
};

TEST(TransactionTypeRegistryTest, RegisterAndLookupFactory) {
	TransactionTypeRegistry reg;
	reg.registerType(0x77, &TtrProbeTx::createFromMmap);
	EXPECT_EQ(reg.getFactory(0x77), &TtrProbeTx::createFromMmap);
}

TEST(TransactionTypeRegistryTest, UnknownTypeReturnsNullFactory) {
	TransactionTypeRegistry reg;
	EXPECT_EQ(reg.getFactory(0x00), nullptr);
	EXPECT_EQ(reg.getFactory(0xFF), nullptr);
}

TEST(TransactionTypeRegistryTest, ReRegisterReplacesFactory) {
	TransactionTypeRegistry reg;
	reg.registerType(0x77, &TtrProbeTx::createFromMmap);
	reg.registerType(0x77, &TtrProbeTx::createFromMmapAlt);
	EXPECT_EQ(reg.getFactory(0x77), &TtrProbeTx::createFromMmapAlt);
}

TEST(TransactionTypeRegistryTest, RegistriesAreIndependent) {
	TransactionTypeRegistry a;
	TransactionTypeRegistry b;
	a.registerType(0x77, &TtrProbeTx::createFromMmap);
	EXPECT_NE(a.getFactory(0x77), nullptr);
	EXPECT_EQ(b.getFactory(0x77), nullptr);
}
