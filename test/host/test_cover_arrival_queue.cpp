#include <string>

#include "doctest/doctest.h"
#include "util/CoverArrivalQueue.h"

TEST_CASE("cover arrival queue takes EPUBs once, oldest first") {
  CoverArrivalQueue q;
  q.push("/Books/a.epub");
  q.push("/Books/notes.txt");
  q.push("/Books/B.EPUB");
  q.push("/Books/a.epub");

  std::string out;
  REQUIRE(q.pop(out));
  CHECK(out == "/Books/a.epub");
  REQUIRE(q.pop(out));
  CHECK(out == "/Books/B.EPUB");
  CHECK_FALSE(q.pop(out));
  CHECK(q.empty());
}

TEST_CASE("cover arrival queue drops the oldest when a bulk upload overflows it") {
  CoverArrivalQueue q;
  for (int i = 0; i < 6; ++i) q.push("/b" + std::to_string(i) + ".epub");

  std::string out;
  REQUIRE(q.pop(out));
  CHECK(out == "/b2.epub");
  int remaining = 1;
  while (q.pop(out)) ++remaining;
  CHECK(remaining == static_cast<int>(CoverArrivalQueue::kCapacity));
  CHECK(out == "/b5.epub");
}
