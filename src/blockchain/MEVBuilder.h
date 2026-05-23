/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-15
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

#ifndef LIBMAGELESSCHAIN_MEVBUILDER_H
#define LIBMAGELESSCHAIN_MEVBUILDER_H


/**
 * @class MEVBuilder
 *
 * @brief Builds blocks to maximize transaction success and "profit", where
 *        profit is defined by data completeness, conciseness, and other factors.
 *
 * In traditional blockchains, MEV (Maximum Extractable Value) refers to the
 * practice of extracting value from transactions by strategically ordering
 * them within a block. In this case, value is found not in financial gain,
 * but in the effectiveness of the on-chain data.
 *
 * This class provides logic to build blocks
 * while prioritizing transaction success, data completeness, and data conciseness.
 *
 * The ideal block includes as many transactions as possible, fails none of them,
 * encodes the data as small as possible, and keeps it effectively indexed.
 *
 * It is the job of the chain state and transaction failure logic to ensure that
 * data inconsistencies cannot be introduced, so this class does not worry about
 * these concerns.
 */
class MEVBuilder {
};


#endif //LIBMAGELESSCHAIN_MEVBUILDER_H