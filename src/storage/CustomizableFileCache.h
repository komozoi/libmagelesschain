
/*
 * Copyright 2021-2026 komozoi
 * Original Creation Date: 2026-5-25
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

#ifndef LIBMAGELESSCHAIN_CUSTOMIZABLEFILECACHE_H
#define LIBMAGELESSCHAIN_CUSTOMIZABLEFILECACHE_H

#include <string>
#include "fcntl.h"
#include <sys/stat.h>

#include <ds/HashMap.h>
#include <fs/FdHandle.h>
#include <alloc/pointer.h>
#include <universaltime.h>


/**
 * A cache that holds both open file descriptors and a runtime object to access the file with
 *
 * This will likely get moved to LibExcessive later
 */
template <class T>
class CustomizableFileCache {
public:
	CustomizableFileCache(std::string root, std::function<sp<T>(const FdHandle&)> builder, unsigned int size = 32)
		: root(std::move(root)), builder(std::move(builder)), openPaths(size) {
		if (this->root.empty() || this->root.back() != '/')
			this->root += '/';
	}

	sp<T> open(const std::string& path, int mode = O_RDWR, int flag = 0660) {
		std::lock_guard _(cacheMutex);
		std::string fullPath = path[0] == '/' ? path : root + path;

		if (openPaths.hasKey(fullPath)) {
			open_path_t& openPath = openPaths.get(fullPath);

			// Return the cached handle if valid
			if (openPath.handle) {
				openPath.lastAccess = millis_since_epoch();
				return openPath.accessor;
			}

			// Remove the invalid handle
			openPaths.remove(fullPath);
		}

		// Open the file first, make sure it succeeded
		FdHandle handle = FdHandle::open(fullPath.c_str(), mode, flag);
		if (!handle)
			return sp<T>();

		// Attempt to create the accessor
		sp<T> accessor = builder(handle);
		addRaw(fullPath, handle, accessor);

		return accessor;
	}

	void add(const std::string& path, const FdHandle& file, sp<T> accessor) {
		std::lock_guard _(cacheMutex);
		addRaw(path, file, accessor);
	}

private:
	void addRaw(const std::string& path, const FdHandle& file, sp<T> accessor) {

		std::string fullPath = path[0] == '/' ? path : root + path;

		if (!file)
			return;

		// Free up a spot in the cache if needed
		if (openPaths.size() >= openPaths.getCapacity()) {
			// Remove one
			int oldest = -1;
			uint64_t oldestTime = UINT64_MAX;
			for (int i = 0; i < openPaths.getCapacity(); i++) {
				open_path_t& openPath = openPaths.valueAtIndex(i);

				// If handle is invalid, remove it first instead of removing an older valid handle
				if (!openPath.handle) {
					oldest = i;
					break;
				}

				// Find the oldest handle, filtering by ones that aren't in use
				if (openPath.lastAccess < oldestTime && openPath.accessor.numReferences() > 1) {
					oldestTime = openPath.lastAccess;
					oldest = i;
				}
			}

			// Remove the oldest handle or the first invalid handle
			if (oldest != -1)
				openPaths.remove(openPaths.keyAtIndex(oldest));
		}

		// Cache the new handle and accessor
		// If we weren't able to remove any from the cache, this will just expand
		// the cache.  It's not ideal but it allows the program to share file data,
		// which is critical to prevent corruption.  If we are expanding the cache,
		// then it probably wasn't big enough anyway.
		openPaths.put(fullPath, {millis_since_epoch(), accessor, file});
	}

	struct open_path_t {
		uint64_t lastAccess = 0;
		sp<T> accessor;
		FdHandle handle;
	};

	std::string root;
	std::function<sp<T>(const FdHandle&)> builder;
	HashMap<std::string, open_path_t> openPaths;
	std::mutex cacheMutex;
};


#endif //LIBMAGELESSCHAIN_CUSTOMIZABLEFILECACHE_H
