/*
 * Project: PainterNode
 * Source:  https://github.com/TBD
 *
 * Copyright (c) 2026 PainterNode Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
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
 * Attribution note (non-binding):
 * If you use this project in a fork or derivative work, we appreciate
 * acknowledgement that it is based on or includes code from PainterNode,
 * or includes a PVM.  Thanks!
 */

#ifndef PAINTERNODE_BLOCKCHAINDOCUMENTSTORE_H
#define PAINTERNODE_BLOCKCHAINDOCUMENTSTORE_H

#include "Document.h"
#include "LongKey.h"
#include "stdint.h"


class BlockchainDocumentStore {
public:
	Document getBlock(uint64_t index) = 0;
	Document getBlock(LongKey<256> hash) = 0;
	Document getTransaction(LongKey<256> hash) = 0;
	Document getDocument(LongKey<256> hash) = 0;

	virtual ~BlockchainDocumentStore() = default;
};

#endif //PAINTERNODE_BLOCKCHAINDOCUMENTSTORE_H