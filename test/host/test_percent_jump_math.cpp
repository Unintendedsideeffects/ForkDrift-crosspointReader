#include <cstddef>
#include <cstdint>
#include <vector>

#include "doctest/doctest.h"

namespace {

struct CumulativeSizes {
  const std::vector<size_t>& values;

  size_t operator()(const int index) const { return values[static_cast<size_t>(index)]; }
};

template <typename CumulativeGetter>
int lowerBoundSpineIndex(const int spineCount, const size_t targetSize, CumulativeGetter&& cumulative) {
  int first = 0;
  int last = spineCount;
  while (first < last) {
    const int middle = first + (last - first) / 2;
    if (targetSize <= cumulative(middle)) {
      last = middle;
    } else {
      first = middle + 1;
    }
  }
  return (first < spineCount) ? first : spineCount - 1;
}

int linearSpineIndex(const std::vector<size_t>& cumulativeSizes, const size_t targetSize) {
  for (int i = 0; i < static_cast<int>(cumulativeSizes.size()); ++i) {
    if (targetSize <= cumulativeSizes[static_cast<size_t>(i)]) {
      return i;
    }
  }
  return static_cast<int>(cumulativeSizes.size()) - 1;
}

size_t targetForPercent(const size_t bookSize, const int percent) {
  size_t target =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    target = bookSize - 1;
  }
  return target;
}

}  // namespace

TEST_CASE("percent jump lower bound handles percent endpoints") {
  const std::vector<size_t> cumulative{10, 20, 30};
  const CumulativeSizes get{cumulative};

  CHECK(lowerBoundSpineIndex(3, targetForPercent(30, 0), get) == 0);
  CHECK(lowerBoundSpineIndex(3, targetForPercent(30, 100), get) == 2);
}

TEST_CASE("percent jump lower bound keeps the old equality edge") {
  const std::vector<size_t> cumulative{10, 20, 30};
  const CumulativeSizes get{cumulative};

  CHECK(lowerBoundSpineIndex(3, 10, get) == 0);
  CHECK(lowerBoundSpineIndex(3, 20, get) == 1);
  CHECK(lowerBoundSpineIndex(3, 30, get) == 2);
}

TEST_CASE("percent jump lower bound handles one spine and zero-size entries") {
  const std::vector<size_t> single{100};
  CHECK(lowerBoundSpineIndex(1, 0, CumulativeSizes{single}) == 0);
  CHECK(lowerBoundSpineIndex(1, 99, CumulativeSizes{single}) == 0);

  const std::vector<size_t> repeated{0, 0, 5, 5, 10};
  const CumulativeSizes get{repeated};
  CHECK(lowerBoundSpineIndex(5, 0, get) == 0);
  CHECK(lowerBoundSpineIndex(5, 5, get) == 2);
  CHECK(lowerBoundSpineIndex(5, 10, get) == 4);
}

TEST_CASE("percent jump lower bound matches the old loop across a randomized sweep") {
  uint32_t state = 0x046u;
  for (int book = 1; book <= 64; ++book) {
    std::vector<size_t> cumulative;
    cumulative.reserve(static_cast<size_t>(book));
    size_t total = 0;
    for (int i = 0; i < book; ++i) {
      state = state * 1664525u + 1013904223u;
      total += static_cast<size_t>((state >> 28) & 0x0fu);
      cumulative.push_back(total);
    }

    const CumulativeSizes get{cumulative};
    for (int percent = 0; percent <= 100; ++percent) {
      const size_t target = targetForPercent(total == 0 ? 1 : total, percent);
      CHECK(lowerBoundSpineIndex(book, target, get) == linearSpineIndex(cumulative, target));
    }
  }
}
