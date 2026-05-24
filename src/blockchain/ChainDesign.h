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

#ifndef LIBMAGELESSCHAIN_CHAINDESIGN_H
#define LIBMAGELESSCHAIN_CHAINDESIGN_H

class BackendRegistry;
class StateOverrideRegistry;
class TransactionTypeRegistry;

/*
 * Application-supplied factory describing the chain's data model.
 *
 * The library owns no inherent chain shape.  At backend construction the
 * design's three registration hooks are called in turn, populating the
 * typed registries the backend then uses for the lifetime of the chain.
 *
 * Registration order is meaningful: indexes, overrides, and transaction
 * types are processed in the order they are registered.  In particular, the
 * order in which override families are sealed at commit time matches the
 * order they were registered in registerOverrides.
 *
 * Lifetime: the ChainDesign instance lives alongside the backend (the
 * application typically keeps a sp<ChainDesign> together with the
 * BlockchainBackend it built).  It is never owned by the library.
 */
class ChainDesign {
public:
	virtual ~ChainDesign() = default;

	/*
	 * Register all Index instances the chain will have.  Each call to
	 * registry.registerIndex<T>(instance) creates a new instance under
	 * the family for type T with a successive uint8_t id starting at 0.
	 */
	virtual void registerIndexes(BackendRegistry& registry) = 0;

	/*
	 * Register the override families that pair with the registered
	 * indexes.  Each call to registry.registerOverride<T>() reserves
	 * one (type, id) slot under the family for type T.
	 *
	 * There must be one override-family registration per index instance
	 * in registerIndexes for the registered indexes to be writable.
	 */
	virtual void registerOverrides(StateOverrideRegistry& registry) = 0;

	/*
	 * Register the transaction subclass factories so the backend can
	 * deserialize Transaction subclasses from the journal.
	 */
	virtual void registerTransactionTypes(TransactionTypeRegistry& registry) = 0;
};

#endif //LIBMAGELESSCHAIN_CHAINDESIGN_H
