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

#ifndef LIBMAGELESSCHAIN_BLOCKCHAININDEX_H
#define LIBMAGELESSCHAIN_BLOCKCHAININDEX_H

#include <cstdint>
#include "ds/Bytestring.h"
#include "ds/ArrayList.h"

class Catalog;

struct segment_coordinate_t {
	uint64_t fileId;
	uint64_t offset;
	uint64_t length;
};

/*
 * Base interface for an application-defined blockchain index.
 *
 * An index it is a lightweight query handle that, at query time, asks the
 * Catalog which segments cover the block range of interest and mmaps just
 * those payloads through the Catalog.  This keeps the
 * working set bounded regardless of chain size: most queries only need a
 * fragment of one or two segments.
 *
 * Lifecycle:
 *   1. The application constructs the index and hands it to the
 *      BackendRegistry via ChainDesign::registerIndexes.
 *   2. The backend immediately calls attach(), giving the index access to
 *      the catalog and its own
 *      indexId coordinate.  The index stores
 *      these and uses them for every subsequent query.
 *   3. At block commit time the backend asks the matching override family
 *      to seal() a payload, writes that payload through the catalog,
 *      and inserts a catalog entry.  Indexes pick up the new segment
 *      lazily on their next query via the catalog.
 *   4. When the catalog reports too many segments, the backend calls
 *      mergeSegments() with the locators it picked for compaction.  The
 *      index returns the merged payload as a single Bytestring; the
 *      backend writes it, catalogs it, and deletes the inputs.
 *
 * Indexes own their queries and decide which segments are relevant for
 * each one.  Every segment present in the catalog authoritatively covers
 * its recorded block range; compaction replaces multiple inputs with one
 * output covering their union and returns the inputs' disk regions to
 * the FreeSpaceFile.
 */
class BlockchainIndex {
public:
	/*
	 * Returns the segment encoding version this index currently emits.
	 * The library stamps each new segment with this version.  When the
	 * index reads back a segment whose recorded version it cannot decode
	 * it should reject the read; the backend may rebuild that block range
	 * from the journal in the future.
	 */
	virtual uint16_t encodingVersion() const = 0;

	/*
	 * Called once during backend construction to wire the index up to its
	 * storage.  After this returns the index can mmap any of its own
	 * segments via the catalog.  The default stores
	 * the parameters as members; subclasses that need additional setup
	 * (caches, etc.) may override and call this base implementation
	 * first.
	 */
	virtual void attach(Catalog* catalog, uint16_t indexId) {
		this->attachedCatalog = catalog;
		this->attachedIndexId = indexId;
	}

	/*
	 * Merge several existing segments into one.  Inputs are passed as
	 * catalog locators (ascending block range, then merge generation).
	 * The index mmaps each input via its attached container, computes the
	 * combined payload covering the union of all input block ranges, and
	 * returns the new payload as a Bytestring.  The backend writes that
	 * payload as a new segment, catalogs it at one merge generation above
	 * the highest input, and deletes the inputs (catalog entries + disk
	 * regions).  Returning an empty Bytestring aborts the merge.
	 */
	virtual Bytestring mergeSegments(const ArrayList<segment_coordinate_t>& inputs) const = 0;

	virtual ~BlockchainIndex() = default;

protected:
	/*
	 * Storage handles wired in by the backend.  Subclasses read these in
	 * their query methods to range-scan the catalog and mmap the relevant
	 * segment payloads.
	 */
	Catalog* attachedCatalog = nullptr;
	uint16_t attachedIndexId = 0;
};

#endif //LIBMAGELESSCHAIN_BLOCKCHAININDEX_H
