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

#include "IndexContainer.h"

#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

static FdHandle openContainerFd(const std::string& filePath) {
	return FdHandle::open(filePath.c_str(), O_RDWR | O_CREAT, 0660);
}

IndexContainer::IndexContainer(const std::string& filePath)
	: file(openContainerFd(filePath)) {
}

uint64_t IndexContainer::writePayload(const Bytestring& payload) {
	if (payload.size() == 0)
		throw std::invalid_argument("IndexContainer: refusing to write empty payload");

	std::lock_guard _(writeMutex);

	uint32_t length = (uint32_t)payload.size();
	off_t offset = file.getFreeRegion(length);
	if ((uint64_t)offset + length > MAX_CONTAINER_BYTES) {
		// Container manager is supposed to gate this; if we land here a
		// caller chose a container that cannot fit the payload.
		file.markFreeRegion(offset, length);
		throw std::overflow_error("IndexContainer::writePayload would exceed MAX_CONTAINER_BYTES; caller picked the wrong container");
	}

	FdHandle& fd = file.getFile();
	fd.seek(offset, SEEK_SET);
	fd.write(&payload[0], length);

	return (uint64_t)offset;
}

IndexContainer::PayloadView IndexContainer::mmapPayload(uint64_t offset, uint64_t length) {
	// mmap() requires the file offset to be a multiple of the page size.
	// FreeSpaceFile makes no such guarantee, so we align the requested
	// offset down to a page boundary, mmap a slightly larger window, and
	// expose `data` pointing back into the mapping at the original byte.
	long pageSize = ::sysconf(_SC_PAGESIZE);
	if (pageSize <= 0) pageSize = 4096;
	uint64_t alignedOffset = offset&  ~(uint64_t)(pageSize - 1);
	uint64_t innerOffset = offset - alignedOffset;
	size_t mappedSize = (size_t)(innerOffset + length);

	FdHandle& fd = file.getFile();
	MmapHandle handle = fd.getMmapHandle((off_t)alignedOffset, mappedSize, PROT_READ, MAP_SHARED);
	const uint8_t* base = handle.directPointer<const uint8_t>(0);

	PayloadView v;
	v.data = base ? base + innerOffset : nullptr;
	v.length = length;
	v.handle = std::move(handle);
	return v;
}

void IndexContainer::freeRegion(uint64_t offset, uint64_t length) {
	std::lock_guard _(writeMutex);
	file.markFreeRegion((off_t)offset, (uint32_t)length);
}

uint64_t IndexContainer::approximateUsedBytes() {
	FdHandle& fd = file.getFile();
	return (uint64_t)fd.seek(0, SEEK_END);
}
