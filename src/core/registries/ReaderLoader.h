#pragma once

#include <HalStorage.h>
#include <HeapGuard.h>
#include <Logging.h>
#include <Memory.h>

#include <memory>
#include <string>
#include <utility>

#include "SpiBusMutex.h"

namespace core {

template <typename T, typename... LoadArgs>
std::unique_ptr<T> loadDocumentNoThrow(const std::string& path, const char* name, LoadArgs&&... loadArgs) {
  SpiBusMutex::Guard guard;
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  if (!heapguard::canAllocate(sizeof(T), 0)) {
    LOG_ERR("READER", "READER_ALLOC_REJECT kind=document type=%s bytes=%u free=%u largest=%u", name,
            static_cast<unsigned int>(sizeof(T)), static_cast<unsigned int>(heapguard::freeBytes()),
            static_cast<unsigned int>(heapguard::largestBlock()));
    return nullptr;
  }
  auto doc = makeUniqueNoThrow<T>(path, "/.crosspoint");
  if (!doc) {
    LOG_ERR("READER", "Failed to allocate %s object", name);
    return nullptr;
  }

  if (!doc->load(std::forward<LoadArgs>(loadArgs)...)) {
    LOG_ERR("READER", "Failed to load %s", name);
    return nullptr;
  }

  return doc;
}

template <typename T, typename... Args>
T* createActivityNoThrow(const char* name, Args&&... args) {
  if (!heapguard::canAllocate(sizeof(T), 0)) {
    LOG_ERR("READER", "READER_ALLOC_REJECT kind=activity type=%s bytes=%u free=%u largest=%u", name,
            static_cast<unsigned int>(sizeof(T)), static_cast<unsigned int>(heapguard::freeBytes()),
            static_cast<unsigned int>(heapguard::largestBlock()));
    return nullptr;
  }
  auto* activity = new (std::nothrow) T(std::forward<Args>(args)...);
  if (!activity) LOG_ERR("READER", "READER_ALLOC_REJECT kind=activity type=%s allocator=null", name);
  return activity;
}

}  // namespace core
