#include <string>

#include "activities/books/BooksTabModel.h"
#include "doctest/doctest.h"

TEST_CASE("Books tab model keeps the approved order and hides reserved slots") {
  const auto tabs = books_tab_model::make("Recent", "Files", "Books", "OPDS", "Settings");

  REQUIRE(tabs.size() == 5);
  CHECK(std::string(tabs[0].label) == "Recent");
  CHECK(std::string(tabs[1].label) == "Files");
  CHECK(tabs[2].enabled);
  CHECK(tabs[3].enabled);
  CHECK_FALSE(tabs[0].enabled);
  CHECK_FALSE(tabs[1].enabled);
  CHECK_FALSE(tabs[4].enabled);
  CHECK(books_tab_model::selectableCount(tabs) == 2);
}

TEST_CASE("Books tab model skips disabled tabs and wraps enabled selection") {
  const auto tabs = books_tab_model::make("Recent", "Files", "Books", "OPDS", "Settings");

  CHECK(books_tab_model::move(tabs, 2, 1) == 3);
  CHECK(books_tab_model::move(tabs, 3, 1) == 2);
  CHECK(books_tab_model::move(tabs, 2, -1) == 3);
  CHECK(books_tab_model::move(tabs, 3, -1) == 2);
  CHECK(books_tab_model::move(tabs, 0, 0) == 2);
}

TEST_CASE("Books tab model safely clamps empty or fully disabled models") {
  CHECK(books_tab_model::move({}, 7, 1) == 0);

  std::vector<books_tab_model::BooksTab> disabled{{"Recent", false}, {"Files", false}};
  CHECK(books_tab_model::move(disabled, 1, 1) == 0);
}
