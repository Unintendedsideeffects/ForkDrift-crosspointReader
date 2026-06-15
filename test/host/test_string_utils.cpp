#include "doctest/doctest.h"
#include "src/util/StringUtils.h"

using namespace StringUtils;

TEST_CASE("StringUtils: sanitizeFilename") {
  // plain ASCII unchanged
  CHECK(sanitizeFilename("book.epub", 100) == "book.epub");

  // illegal chars replaced with _
  CHECK(sanitizeFilename("a/b:c*d?e", 100) == "a_b_c_d_e");

  // leading spaces/dots stripped
  CHECK(sanitizeFilename("  ..name", 100) == "name");

  // trailing spaces/dots trimmed
  CHECK(sanitizeFilename("name.. ", 100) == "name");

  // empty / all-stripped input falls back to "book"
  CHECK(sanitizeFilename("", 100) == "book");
  CHECK(sanitizeFilename("...", 100) == "book");

  // byte-limit truncation does not split a multibyte codepoint
  // "é" in UTF-8 is 0xC3 0xA9 (2 bytes)
  std::string utf8_input = "éééé";
  std::string result_3 = sanitizeFilename(utf8_input, 3);
  CHECK(result_3 == "é");
  CHECK(result_3.size() == 2);
  CHECK(result_3.size() <= 3);

  std::string result_2 = sanitizeFilename(utf8_input, 2);
  CHECK(result_2 == "é");
  CHECK(result_2.size() == 2);

  std::string result_1 = sanitizeFilename(utf8_input, 1);
  CHECK(result_1 == "book");
}
