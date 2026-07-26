#pragma once

#include <HalStorage.h>
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

}  // namespace core
