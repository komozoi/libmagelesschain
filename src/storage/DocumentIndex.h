/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 8
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

#ifndef LIBMAGELESSCHAIN_DOCUMENTINDEX_H
#define LIBMAGELESSCHAIN_DOCUMENTINDEX_H

#include <string>

#include "ds/PriorityQueue.h"
#include "fs/BTree.h"
#include "fs/FdHandle.h"
#include "fs/FreeSpaceFile.h"


class DocumentIndexSegmentFile: public FreeSpaceFile {
public:
	explicit DocumentIndexSegmentFile(std::string path);

private:
	struct segment_header_t {
		uint32_t magic;
		uint32_t version;
	};

	struct docid_btree_entry_t {
		uint64_t docId;
		uint64_t blockNumber;

		static int compare(const docid_btree_entry_t& a, const docid_btree_entry_t& b) {
			return a.docId > b.docId ? 1 : a.docId < b.docId ? -1 : 0;
		}
	};

	struct time_btree_entry_t {
		uint64_t millis;
		uint64_t blockNumber;

		static int compare(const time_btree_entry_t& a, const time_btree_entry_t& b) {
			return a.millis > b.millis ? 1 : a.millis < b.millis ? -1 : 0;
		}
	};

	segment_header_t* header;
	MmapHandle headerHandle;

	BTree<docid_btree_entry_t> docIdBtree;
	BTree<time_btree_entry_t> timeBtree;
};


class DocumentIndexMapFile: public FreeSpaceFile {
public:
	explicit DocumentIndexMapFile(std::string path);

private:
	struct map_header_t {
		uint32_t magic;
		uint32_t version;
	};

	struct docid_btree_entry_t {
		uint64_t docId;
		uint64_t blockNumber;

		static int compare(const docid_btree_entry_t& a, const docid_btree_entry_t& b) {
			return a.docId > b.docId ? 1 : a.docId < b.docId ? -1 : 0;
		}
	};

	struct time_range_btree_entry_t {
		uint64_t startMillis;
		uint64_t endMillis;
		uint32_t segmentFile;
		uint32_t reserved;

		static int compare(const time_range_btree_entry_t& a, const time_range_btree_entry_t& b) {
			if (a.endMillis <= b.startMillis)
				return -1;
			if (a.startMillis >= b.endMillis)
				return 1;

			// At this point, the intervals overlap - this shouldn't really happen...
			
			return a.startMillis > b.startMillis ? 1 : a.startMillis < b.startMillis ? -1 : 0;
		}
	};

	map_header_t* header;
	MmapHandle headerHandle;

	BTree<docid_btree_entry_t> docIdBtree;
	BTree<time_range_btree_entry_t> timeBtree;
};


class DocumentIndex {
public:
	explicit DocumentIndex(std::string datadir);

private:
	std::string datadir;

	DocumentIndexMapFile indexMap;
	StaticPriorityQueue<DocumentIndexSegmentFile, 128> openSegments;
};


#endif //LIBMAGELESSCHAIN_DOCUMENTINDEX_H