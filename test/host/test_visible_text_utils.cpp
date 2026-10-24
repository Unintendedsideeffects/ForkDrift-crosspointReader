#include "Epub/VisibleTextUtils.h"
#include "doctest/doctest.h"

TEST_CASE("VisibleTextUtils skips the resolver non-visible set including rt") {
  CHECK(VisibleTextUtils::isNonVisibleElement("head"));
  CHECK(VisibleTextUtils::isNonVisibleElement("HEAD"));
  CHECK(VisibleTextUtils::isNonVisibleElement("style"));
  CHECK(VisibleTextUtils::isNonVisibleElement("script"));
  CHECK(VisibleTextUtils::isNonVisibleElement("title"));
  CHECK(VisibleTextUtils::isNonVisibleElement("rp"));
  CHECK(VisibleTextUtils::isNonVisibleElement("RT"));
  CHECK_FALSE(VisibleTextUtils::isNonVisibleElement("ruby"));
  CHECK_FALSE(VisibleTextUtils::isNonVisibleElement("p"));
  CHECK_FALSE(VisibleTextUtils::isNonVisibleElement("body"));
}
