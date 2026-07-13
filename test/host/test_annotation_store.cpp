#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "src/util/AnnotationStore.h"
#include "test/mock/HalStorage.h"

TEST_CASE("AnnotationStore round-trip loading and saving") {
  Storage.reset();
  auto& store = AnnotationStore::getInstance();

  CHECK(store.loadForBook("/cache_roundtrip"));
  CHECK(store.all().empty());

  Annotation a1{1, 2, 5, 8, "first annotation"};
  Annotation a2{1, 3, 10, 15, "second annotation"};
  Annotation a3{2, 1, 0, 4, "third annotation"};

  CHECK(store.add(a1));
  CHECK(store.add(a2));
  CHECK(store.add(a3));

  store.unload();

  CHECK(Storage.exists("/cache_roundtrip/annotations.bin"));

  CHECK(store.loadForBook("/cache_roundtrip"));
  REQUIRE(store.all().size() == 3);
  CHECK(store.all()[0].spineIndex == 1);
  CHECK(store.all()[0].page == 2);
  CHECK(store.all()[0].startWord == 5);
  CHECK(store.all()[0].endWord == 8);
  CHECK(store.all()[0].text == "first annotation");

  CHECK(store.all()[1].spineIndex == 1);
  CHECK(store.all()[1].page == 3);
  CHECK(store.all()[1].startWord == 10);
  CHECK(store.all()[1].endWord == 15);
  CHECK(store.all()[1].text == "second annotation");

  CHECK(store.all()[2].spineIndex == 2);
  CHECK(store.all()[2].page == 1);
  CHECK(store.all()[2].startWord == 0);
  CHECK(store.all()[2].endWord == 4);
  CHECK(store.all()[2].text == "third annotation");

  store.unload();
}

TEST_CASE("AnnotationStore crash recovery promotes surviving temp") {
  Storage.reset();
  auto& store = AnnotationStore::getInstance();

  CHECK(store.loadForBook("/cache_recovery"));
  Annotation a1{1, 2, 5, 8, "first annotation"};
  Annotation a2{1, 3, 10, 15, "second annotation"};
  CHECK(store.add(a1));
  CHECK(store.add(a2));
  store.unload();

  REQUIRE(Storage.rename("/cache_recovery/annotations.bin", "/cache_recovery/annotations.bin.tmp"));
  CHECK(Storage.exists("/cache_recovery/annotations.bin") == false);
  CHECK(Storage.exists("/cache_recovery/annotations.bin.tmp"));

  CHECK(store.loadForBook("/cache_recovery"));
  REQUIRE(store.all().size() == 2);
  CHECK(store.all()[0].text == "first annotation");
  CHECK(store.all()[1].text == "second annotation");
  CHECK(Storage.exists("/cache_recovery/annotations.bin"));
  CHECK(Storage.exists("/cache_recovery/annotations.bin.tmp") == false);

  store.unload();
}

TEST_CASE("AnnotationStore stale temp does not replace valid file") {
  Storage.reset();
  auto& store = AnnotationStore::getInstance();

  CHECK(store.loadForBook("/cache_stale_temp"));
  Annotation valid{2, 4, 1, 3, "valid annotation"};
  CHECK(store.add(valid));
  store.unload();

  REQUIRE(Storage.writeFile("/cache_stale_temp/annotations.bin.tmp", "stale temp content"));
  CHECK(store.loadForBook("/cache_stale_temp"));

  REQUIRE(store.all().size() == 1);
  CHECK(store.all()[0].text == "valid annotation");
  CHECK(Storage.exists("/cache_stale_temp/annotations.bin"));
  CHECK(Storage.exists("/cache_stale_temp/annotations.bin.tmp") == false);

  store.unload();
}

TEST_CASE("AnnotationStore removeAt range semantics") {
  Storage.reset();
  auto& store = AnnotationStore::getInstance();

  CHECK(store.loadForBook("/cache_remove"));

  Annotation a{1, 2, 5, 8, "test range"};
  CHECK(store.add(a));

  // Word indices outside range should not remove the annotation
  CHECK(store.removeAt(1, 2, 4) == 0);
  CHECK(store.removeAt(1, 2, 9) == 0);
  CHECK(store.all().size() == 1);

  // Different spine or page should not remove it
  CHECK(store.removeAt(2, 2, 6) == 0);
  CHECK(store.removeAt(1, 3, 6) == 0);
  CHECK(store.all().size() == 1);

  // Remove Candidate check
  CHECK(store.removeCandidateAt(1, 2, 6) == true);
  CHECK(store.removeCandidateAt(1, 2, 4) == false);

  // Word index inside range (inclusive) should remove it
  // We'll test with 6, which is inside [5..8]
  CHECK(store.removeAt(1, 2, 6) == 1);
  CHECK(store.all().empty());

  // Test start index boundary
  CHECK(store.add(a));
  CHECK(store.removeAt(1, 2, 5) == 1);
  CHECK(store.all().empty());

  // Test end index boundary
  CHECK(store.add(a));
  CHECK(store.removeAt(1, 2, 8) == 1);
  CHECK(store.all().empty());

  store.unload();
}

TEST_CASE("AnnotationStore forPage/hasAnyFor filtering") {
  Storage.reset();
  auto& store = AnnotationStore::getInstance();

  CHECK(store.loadForBook("/cache_filter"));

  Annotation a1{1, 2, 5, 8, "spine 1 page 2"};
  Annotation a2{1, 2, 10, 12, "spine 1 page 2 second"};
  Annotation a3{1, 3, 0, 4, "spine 1 page 3"};

  CHECK(store.add(a1));
  CHECK(store.add(a2));
  CHECK(store.add(a3));

  CHECK(store.hasAnyFor(1, 2) == true);
  CHECK(store.hasAnyFor(1, 3) == true);
  CHECK(store.hasAnyFor(1, 4) == false);
  CHECK(store.hasAnyFor(2, 2) == false);

  auto page1_2 = store.forPage(1, 2);
  REQUIRE(page1_2.size() == 2);
  CHECK(page1_2[0]->text == "spine 1 page 2");
  CHECK(page1_2[1]->text == "spine 1 page 2 second");

  auto page1_3 = store.forPage(1, 3);
  REQUIRE(page1_3.size() == 1);
  CHECK(page1_3[0]->text == "spine 1 page 3");

  auto page2_2 = store.forPage(2, 2);
  CHECK(page2_2.empty());

  store.unload();
}

TEST_CASE("AnnotationStore forSpine ignores page hints") {
  Storage.reset();
  auto& store = AnnotationStore::getInstance();

  CHECK(store.loadForBook("/cache_spine"));

  Annotation a1{1, 2, 5, 8, "spine 1 page 2"};
  Annotation a2{1, 7, 10, 12, "spine 1 page 7"};
  Annotation a3{2, 2, 0, 4, "spine 2 page 2"};

  CHECK(store.add(a1));
  CHECK(store.add(a2));
  CHECK(store.add(a3));

  auto spine1 = store.forSpine(1);
  REQUIRE(spine1.size() == 2);
  CHECK(spine1[0]->text == "spine 1 page 2");
  CHECK(spine1[1]->text == "spine 1 page 7");

  auto spine2 = store.forSpine(2);
  REQUIRE(spine2.size() == 1);
  CHECK(spine2[0]->text == "spine 2 page 2");

  auto spine3 = store.forSpine(3);
  CHECK(spine3.empty());

  store.unload();
}

TEST_CASE("AnnotationStore updateHints persists healed page and word hints") {
  Storage.reset();
  auto& store = AnnotationStore::getInstance();

  CHECK(store.loadForBook("/cache_heal"));

  Annotation stale{4, 5, 12, 15, "healed annotation"};
  CHECK(store.add(stale));

  auto before = store.forSpine(4);
  REQUIRE(before.size() == 1);
  CHECK(store.updateHints(before[0], 7, 2, 5));
  CHECK(store.all()[0].page == 7);
  CHECK(store.all()[0].startWord == 2);
  CHECK(store.all()[0].endWord == 5);

  store.saveToFile();
  store.unload();

  CHECK(store.loadForBook("/cache_heal"));
  auto after = store.forSpine(4);
  REQUIRE(after.size() == 1);
  CHECK(after[0]->page == 7);
  CHECK(after[0]->startWord == 2);
  CHECK(after[0]->endWord == 5);
  CHECK(after[0]->text == "healed annotation");

  store.unload();
}

TEST_CASE("AnnotationStore caps enforced") {
  Storage.reset();
  auto& store = AnnotationStore::getInstance();

  CHECK(store.loadForBook("/cache_caps"));

  // Check text size caps
  std::string exactMaxText(AnnotationStore::kMaxTextBytes, 'a');
  Annotation aMaxText{1, 2, 0, 5, exactMaxText};
  CHECK(store.add(aMaxText));

  // Remove it to clear the store
  CHECK(store.removeAt(1, 2, 0) == 1);

  std::string overMaxText(AnnotationStore::kMaxTextBytes + 1, 'b');
  Annotation aOverMaxText{1, 2, 0, 5, overMaxText};
  CHECK(store.add(aOverMaxText) == false);

  // Empty string text cap
  Annotation aEmptyText{1, 2, 0, 5, ""};
  CHECK(store.add(aEmptyText) == false);

  // Check annotations count caps
  for (size_t i = 0; i < AnnotationStore::kMaxAnnotationsPerBook; ++i) {
    Annotation temp{1, 2, 0, 5, "a"};
    CHECK(store.add(temp));
  }
  CHECK(store.all().size() == AnnotationStore::kMaxAnnotationsPerBook);

  // Adding 201st should fail
  Annotation extra{1, 2, 0, 5, "extra"};
  CHECK(store.add(extra) == false);
  CHECK(store.all().size() == AnnotationStore::kMaxAnnotationsPerBook);

  store.unload();
}

TEST_CASE("AnnotationStore corrupt file handling") {
  Storage.reset();
  auto& store = AnnotationStore::getInstance();

  Storage.writeFile("/cache_corrupt/annotations.bin", "garbage bytes here");
  CHECK(store.loadForBook("/cache_corrupt") == true);
  CHECK(store.all().empty());

  store.unload();
}

TEST_CASE("AnnotationStore truncated file handling") {
  Storage.reset();
  auto& store = AnnotationStore::getInstance();

  CHECK(store.loadForBook("/cache_trunc"));
  Annotation a1{1, 2, 5, 8, "first annotation"};
  Annotation a2{1, 3, 10, 15, "second annotation"};
  Annotation a3{2, 1, 0, 4, "third annotation"};
  CHECK(store.add(a1));
  CHECK(store.add(a2));
  CHECK(store.add(a3));
  store.unload();

  // Open the file and truncate it to half its size
  FsFile file;
  REQUIRE(Storage.openFileForRead("ANN", "/cache_trunc/annotations.bin", file));
  size_t originalSize = file.size();
  std::vector<uint8_t> buffer(originalSize);
  REQUIRE(file.read(buffer.data(), originalSize) == originalSize);
  file.close();

  size_t halfSize = originalSize / 2;
  REQUIRE(Storage.openFileForWrite("ANN", "/cache_trunc/annotations.bin", file));
  REQUIRE(file.write(buffer.data(), halfSize) == halfSize);
  file.close();

  // Load and verify no crash, load partial clean entries
  CHECK(store.loadForBook("/cache_trunc"));
  // It should load 0, 1 or 2 entries, but definitely less than 3, and no crash
  CHECK(store.all().size() < 3);
  for (const auto& entry : store.all()) {
    CHECK(!entry.text.empty());
  }

  store.unload();
}

TEST_CASE("AnnotationStore stale temp file cleanup") {
  Storage.reset();
  auto& store = AnnotationStore::getInstance();

  Storage.writeFile("/cache_temp/annotations.bin.tmp", "stale temp content");
  CHECK(Storage.exists("/cache_temp/annotations.bin.tmp") == true);

  CHECK(store.loadForBook("/cache_temp"));
  // Temp file should be removed on load
  CHECK(Storage.exists("/cache_temp/annotations.bin.tmp") == false);

  Annotation a{1, 2, 0, 5, "valid"};
  CHECK(store.add(a));
  store.unload();

  CHECK(Storage.exists("/cache_temp/annotations.bin") == true);
  CHECK(store.loadForBook("/cache_temp"));
  REQUIRE(store.all().size() == 1);
  CHECK(store.all()[0].text == "valid");

  store.unload();
}
