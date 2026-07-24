#include <string>

#include "activities/books/BooksTabModel.h"
#include "doctest/doctest.h"

TEST_CASE("Books tab model is the unified library strip with all tabs selectable") {
  const auto tabs = books_tab_model::make("Recent", "Files", "OPDS", "Settings");

  REQUIRE(tabs.size() == 4);
  CHECK(std::string(tabs[0].label) == "Recent");
  CHECK(std::string(tabs[1].label) == "Files");
  CHECK(std::string(tabs[2].label) == "OPDS");
  CHECK(std::string(tabs[3].label) == "Settings");
  CHECK(tabs[0].enabled);
  CHECK(tabs[1].enabled);
  CHECK(tabs[2].enabled);
  CHECK(tabs[3].enabled);
  CHECK(books_tab_model::selectableCount(tabs) == 4);
}

TEST_CASE("Books tab model moves through all enabled tabs and wraps") {
  const auto tabs = books_tab_model::make("Recent", "Files", "OPDS", "Settings");

  CHECK(books_tab_model::move(tabs, 0, 1) == 1);
  CHECK(books_tab_model::move(tabs, 3, 1) == 0);   // wrap forward
  CHECK(books_tab_model::move(tabs, 0, -1) == 3);  // wrap backward
  CHECK(books_tab_model::move(tabs, 2, -1) == 1);
}

TEST_CASE("Books tab model safely clamps empty or fully disabled models") {
  CHECK(books_tab_model::move({}, 7, 1) == 0);

  std::vector<books_tab_model::BooksTab> disabled{{"Recent", false}, {"Files", false}};
  CHECK(books_tab_model::move(disabled, 1, 1) == 0);
}
