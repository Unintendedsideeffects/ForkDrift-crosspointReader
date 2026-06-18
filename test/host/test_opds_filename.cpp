#include "doctest/doctest.h"
#include "util/OpdsFilename.h"

TEST_CASE("OPDS filename formats author and title orders") {
  CHECK(OpdsFilename::format("The Book", "Ada", OpdsFilename::Format::AuthorTitle, ".epub") == "Ada - The Book.epub");
  CHECK(OpdsFilename::format("The Book", "Ada", OpdsFilename::Format::TitleAuthor, "epub") == "The Book - Ada.epub");
}

TEST_CASE("OPDS filename sanitizes path separators and punctuation") {
  CHECK(OpdsFilename::format("Bad/Book:Name", "A?B*", OpdsFilename::Format::TitleAuthor, ".epub") ==
        "Bad_Book_Name - A_B_.epub");
}

TEST_CASE("OPDS filename falls back deterministically") {
  CHECK(OpdsFilename::format("", "", OpdsFilename::Format::AuthorTitle, ".epub") == "book.epub");
  CHECK(OpdsFilename::format("Title", "", OpdsFilename::Format::AuthorTitle, "") == "Title");
  CHECK(OpdsFilename::format(" . ", "Author", OpdsFilename::Format::AuthorTitle, ".epub") == "Author - book.epub");
}
