#include <cstdint>
#include <optional>

#include "Epub/VisibleTextOffsetLut.h"
#include "doctest/doctest.h"

TEST_CASE("visible-text LUT: empty or missing table has no page") {
  CHECK_FALSE(visible_text_lut::pageForOffset(nullptr, 3, 0, false).has_value());
  const uint32_t starts[] = {0};
  CHECK_FALSE(visible_text_lut::pageForOffset(starts, 0, 0, false).has_value());
}

TEST_CASE("visible-text LUT: clamp past the last page") {
  const uint32_t starts[] = {0, 40, 90};
  CHECK(visible_text_lut::pageForOffset(starts, 3, 0, false) == 0);
  CHECK(visible_text_lut::pageForOffset(starts, 3, 39, false) == 0);
  CHECK(visible_text_lut::pageForOffset(starts, 3, 40, false) == 1);
  CHECK(visible_text_lut::pageForOffset(starts, 3, 5000, false) == 2);
}

TEST_CASE("visible-text LUT: prefer the first page that starts at the offset") {
  const uint32_t starts[] = {10, 10, 25};
  CHECK(visible_text_lut::pageForOffset(starts, 3, 10, false) == 1);
  CHECK(visible_text_lut::pageForOffset(starts, 3, 10, true) == 0);
  CHECK(visible_text_lut::pageForOffset(starts, 3, 9, true) == 0);
}
