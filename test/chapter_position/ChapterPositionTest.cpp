#include <gtest/gtest.h>

#include "src/activities/reader/ChapterPosition.h"

TEST(ChapterPositionTest, DisplayPageIsOneBased) {
  EXPECT_EQ((ChapterPosition{0, 48}).displayPage(), 1);
  EXPECT_EQ((ChapterPosition{10, 48}).displayPage(), 11);
  EXPECT_EQ((ChapterPosition{47, 48}).displayPage(), 48);
}

TEST(ChapterPositionTest, CachedPositionKeepsChapterClause) {
  const ChapterPosition cached{10, 48};
  EXPECT_TRUE(cached.hasTotal());
  EXPECT_EQ(cached.displayPage(), 11);
  EXPECT_EQ(cached.totalPages, 48);
}

TEST(ChapterPositionTest, ChapterFractionUsesZeroBasedIndex) {
  EXPECT_FLOAT_EQ((ChapterPosition{0, 48}).chapterFraction(), 0.0f);
  EXPECT_FLOAT_EQ((ChapterPosition{10, 48}).chapterFraction(), 10.0f / 48.0f);
  EXPECT_FLOAT_EQ((ChapterPosition{47, 48}).chapterFraction(), 47.0f / 48.0f);
}

TEST(ChapterPositionTest, UnknownTotalHasNoFraction) {
  const ChapterPosition unknown{5, 0};
  EXPECT_FALSE(unknown.hasTotal());
  EXPECT_FLOAT_EQ(unknown.chapterFraction(), 0.0f);
  EXPECT_FALSE((ChapterPosition{}).hasTotal());
}

TEST(ChapterPositionTest, EvaluatesAtCompileTime) {
  static_assert(ChapterPosition{10, 48}.displayPage() == 11);
  static_assert(ChapterPosition{10, 48}.hasTotal());
  static_assert(!ChapterPosition{10, 0}.hasTotal());
}
