#pragma once

namespace core {

class FeatureCatalog {
 public:
  static bool isEnabled(const char* /*key*/) { return true; }
};

}  // namespace core
