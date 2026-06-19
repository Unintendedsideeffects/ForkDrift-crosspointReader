#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class DictionaryAccessor {
 public:
  virtual ~DictionaryAccessor() = default;
  virtual bool seek(size_t pos) = 0;
  virtual size_t read(void* buf, size_t size) = 0;
  virtual size_t size() const = 0;
};

class DictionaryLookup {
 public:
  // Returns the definition string if found, otherwise an empty string.
  static std::string lookup(DictionaryAccessor& accessor, const std::string& query);

 private:
  static std::string caseFold(const std::string& input);
};
