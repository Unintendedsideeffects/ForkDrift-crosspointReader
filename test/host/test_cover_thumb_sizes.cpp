#include "doctest/doctest.h"
#include "src/util/CoverThumbSizes.h"

TEST_CASE("cover thumb size registry is non-empty and unique") {
  coverthumbs::Size sizes[8] = {};
  const int count = coverthumbs::all(sizes, 8);

  CHECK(count > 0);
  CHECK(count <= 8);

  for (int i = 0; i < count; ++i) {
    CHECK(sizes[i].height > 0);
    CHECK(sizes[i].width >= 0);
    for (int j = i + 1; j < count; ++j) {
      const bool sameSize = sizes[i].width == sizes[j].width && sizes[i].height == sizes[j].height;
      CHECK_FALSE(sameSize);
    }
  }
}
