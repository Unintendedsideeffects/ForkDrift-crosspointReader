#include <cstring>
#include <sstream>
#include <vector>

#include "doctest/doctest.h"
#include "util/DictionaryLookup.h"

class StringDictionaryAccessor : public DictionaryAccessor {
 public:
  explicit StringDictionaryAccessor(const std::string& data) : data_(data), pos_(0) {}

  bool seek(size_t pos) override {
    if (pos > data_.size()) return false;
    pos_ = pos;
    return true;
  }

  size_t read(void* buf, size_t size) override {
    if (pos_ >= data_.size()) return 0;
    size_t available = data_.size() - pos_;
    size_t toRead = std::min(size, available);
    std::memcpy(buf, data_.data() + pos_, toRead);
    pos_ += toRead;
    return toRead;
  }

  size_t size() const override { return data_.size(); }

 private:
  std::string data_;
  size_t pos_;
};

TEST_CASE("DictionaryLookup binary search tests") {
  std::string dictData =
      "apple\tA fruit.\n"
      "banana\tA yellow fruit.\n"
      "cherry\tA red fruit.\n"
      "date\tA sweet fruit.\n"
      "elderberry\tA small dark berry.\n"
      "fig\tA soft sweet fruit.\n"
      "grape\tA small round fruit.\n";

  StringDictionaryAccessor accessor(dictData);

  SUBCASE("Found at start") { REQUIRE(DictionaryLookup::lookup(accessor, "apple") == "A fruit."); }

  SUBCASE("Found in middle") { REQUIRE(DictionaryLookup::lookup(accessor, "date") == "A sweet fruit."); }

  SUBCASE("Found at end") { REQUIRE(DictionaryLookup::lookup(accessor, "grape") == "A small round fruit."); }

  SUBCASE("Not found before first") { REQUIRE(DictionaryLookup::lookup(accessor, "aardvark") == ""); }

  SUBCASE("Not found after last") { REQUIRE(DictionaryLookup::lookup(accessor, "zebra") == ""); }

  SUBCASE("Not found in middle") { REQUIRE(DictionaryLookup::lookup(accessor, "coconut") == ""); }

  SUBCASE("Case folding") {
    REQUIRE(DictionaryLookup::lookup(accessor, "APPLE") == "A fruit.");
    REQUIRE(DictionaryLookup::lookup(accessor, "Banana") == "A yellow fruit.");
  }
}

TEST_CASE("DictionaryLookup empty file") {
  StringDictionaryAccessor accessor("");
  REQUIRE(DictionaryLookup::lookup(accessor, "apple") == "");
}

TEST_CASE("DictionaryLookup file with single line without trailing newline") {
  StringDictionaryAccessor accessor("apple\tA fruit.");
  REQUIRE(DictionaryLookup::lookup(accessor, "apple") == "A fruit.");
}

TEST_CASE("DictionaryLookup file with Windows line endings") {
  StringDictionaryAccessor accessor("apple\tA fruit.\r\nbanana\tA yellow fruit.\r\n");
  REQUIRE(DictionaryLookup::lookup(accessor, "banana") == "A yellow fruit.");
}
