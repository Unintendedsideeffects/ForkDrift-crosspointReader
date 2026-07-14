#include <string>

#include "doctest/doctest.h"
#include "src/util/InputValidation.h"
#include "src/util/PathUtils.h"

TEST_CASE("testInputValidation") {
  size_t index = 0;
  CHECK(!InputValidation::findAsciiControlChar("/ok/path", 8, index));

  const std::string hasNewline = "/bad\npath";
  CHECK(InputValidation::findAsciiControlChar(hasNewline.c_str(), hasNewline.size(), index));
  CHECK(index == 4);

  const std::string hasDel = std::string("abc") + static_cast<char>(0x7F) + "def";
  CHECK(InputValidation::findAsciiControlChar(hasDel.c_str(), hasDel.size(), index));
  CHECK(index == 3);

  const std::string hasNull = std::string("ab\0cd", 5);
  CHECK(InputValidation::findAsciiControlChar(hasNull.c_str(), hasNull.size(), index));
  CHECK(index == 2);

  size_t parsed = 0;
  CHECK((InputValidation::parseStrictPositiveSize("1", 1, 100, parsed) && parsed == 1));
  CHECK((InputValidation::parseStrictPositiveSize("512", 3, 512, parsed) && parsed == 512));
  CHECK((InputValidation::parseStrictPositiveSize("0010", 4, 100, parsed) && parsed == 10));
  CHECK((InputValidation::parseStrictPositiveSize("536870912", 9, 536870912ULL, parsed) && parsed == 536870912ULL));

  CHECK(!InputValidation::parseStrictPositiveSize("", 0, 100, parsed));
  CHECK(!InputValidation::parseStrictPositiveSize("0", 1, 100, parsed));
  CHECK(!InputValidation::parseStrictPositiveSize("101", 3, 100, parsed));
  CHECK(!InputValidation::parseStrictPositiveSize("536870913", 9, 536870912ULL, parsed));
  CHECK(!InputValidation::parseStrictPositiveSize("12a", 3, 100, parsed));
  CHECK(!InputValidation::parseStrictPositiveSize(nullptr, 0, 100, parsed));

  const std::string huge = "184467440737095516161844674407370955161";
  CHECK(!InputValidation::parseStrictPositiveSize(huge.c_str(), huge.size(), static_cast<size_t>(-1), parsed));
}

TEST_CASE("testPathUtilsSecurity") {
  // ── containsTraversal ────────────────────────────────────────────────
  CHECK(PathUtils::containsTraversal("/../secret"));
  CHECK(PathUtils::containsTraversal("/books/.."));
  CHECK(PathUtils::containsTraversal("../etc/passwd"));
  CHECK(PathUtils::containsTraversal(".."));
  CHECK(PathUtils::containsTraversal("%2e%2e%2f"));
  CHECK(PathUtils::containsTraversal("/foo/%2f%2e%2e"));
  CHECK(PathUtils::containsTraversal("/foo/..%2fbar"));
  CHECK(PathUtils::containsTraversal("/foo/%2f../bar"));
  CHECK(!PathUtils::containsTraversal("/books/my..chapter"));
  CHECK(!PathUtils::containsTraversal("/valid/path"));

  // ── isValidSdPath ────────────────────────────────────────────────────
  CHECK(PathUtils::isValidSdPath("/books/novel.epub"));
  CHECK(PathUtils::isValidSdPath("/"));
  CHECK(!PathUtils::isValidSdPath(""));
  CHECK(!PathUtils::isValidSdPath("/../secret"));
  {
    String longPath("/");
    for (int i = 0; i < 260; ++i) longPath += 'a';
    CHECK(!PathUtils::isValidSdPath(longPath));
  }
  {
    String ctrlPath("/bad");
    ctrlPath += '\n';
    CHECK(!PathUtils::isValidSdPath(ctrlPath));
  }
  CHECK(!PathUtils::isValidSdPath("/bad\\path"));

  // ── normalizePath ────────────────────────────────────────────────────
  CHECK(PathUtils::normalizePath("") == "/");
  CHECK(PathUtils::normalizePath("books") == "/books");
  CHECK(PathUtils::normalizePath("/books/") == "/books");
  CHECK(PathUtils::normalizePath("//books//sub") == "/books/sub");
  CHECK(PathUtils::normalizePath("/") == "/");

  // ── urlDecode ────────────────────────────────────────────────────────
  CHECK(PathUtils::urlDecode("/hello%20world") == "/hello world");
  CHECK(PathUtils::urlDecode("/a+b") == "/a b");
  CHECK(PathUtils::urlDecode("/no%2Fslash") == "/no/slash");
  CHECK(PathUtils::urlDecode("/%") == "/%");  // invalid escape kept
  CHECK(PathUtils::urlDecode("/plain") == "/plain");

  // ── isValidFilename ──────────────────────────────────────────────────
  CHECK(PathUtils::isValidFilename("book.epub"));
  CHECK(PathUtils::isValidFilename("my novel.txt"));
  CHECK(!PathUtils::isValidFilename(""));
  CHECK(!PathUtils::isValidFilename("bad/name"));
  CHECK(!PathUtils::isValidFilename("bad\\name"));
  CHECK(!PathUtils::isValidFilename("bad:name"));
  CHECK(!PathUtils::isValidFilename("bad<name"));
  CHECK(!PathUtils::isValidFilename("bad>name"));
  CHECK(!PathUtils::isValidFilename("bad?name"));
  CHECK(!PathUtils::isValidFilename("bad|name"));
  CHECK(!PathUtils::isValidFilename("bad*name"));
  CHECK(!PathUtils::isValidFilename("bad\"name"));
  CHECK(!PathUtils::isValidFilename("."));
  CHECK(!PathUtils::isValidFilename(".."));
  {
    String longName;
    for (int i = 0; i < 260; ++i) longName += 'a';
    CHECK(!PathUtils::isValidFilename(longName));
  }
}
