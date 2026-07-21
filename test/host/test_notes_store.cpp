#include <FeatureFlags.h>

#include "doctest/doctest.h"
#include "src/util/NotesStore.h"

TEST_CASE("NotesStore bookNotesPath sanitizes titles") {
  CHECK(NotesStore::bookNotesPath("My Book!") == "/Notes/My Book_.md");
  CHECK(NotesStore::bookNotesPath("") == "/Notes/untitled.md");
  CHECK(NotesStore::bookNotesPath("....") == "/Notes/untitled.md");
}
