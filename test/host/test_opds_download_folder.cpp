#include <string>

#include "OpdsServerStore.h"
#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"
#include "util/OpdsDownloadFolder.h"

namespace {
void resetOpdsStore() {
  Storage.reset();
  Storage.mkdir("/.crosspoint");
  REQUIRE(Storage.writeFile("/.crosspoint/opds.json", "{\"servers\":[]}"));
  OPDS_STORE.markDirty();
  REQUIRE(OPDS_STORE.loadFromFile());
  REQUIRE(OPDS_STORE.getCount() == 0);
}
}  // namespace

TEST_CASE("normalize empty and slash encodings") {
  std::string out;
  REQUIRE(OpdsDownloadFolder::normalize("", out));
  CHECK(out == "");
  REQUIRE(OpdsDownloadFolder::normalize("/", out));
  CHECK(out == "/");
  REQUIRE(OpdsDownloadFolder::normalize(" /Books//Calibre/ ", out));
  CHECK(out == "/Books/Calibre");
}

TEST_CASE("normalize rejects traversal and illegal characters") {
  std::string out = "keep";
  CHECK_FALSE(OpdsDownloadFolder::normalize("..", out));
  CHECK_FALSE(OpdsDownloadFolder::normalize("/Books/../etc", out));
  CHECK_FALSE(OpdsDownloadFolder::normalize("a:b", out));
  CHECK_FALSE(OpdsDownloadFolder::normalize("/Books.", out));
  CHECK_FALSE(OpdsDownloadFolder::normalize(std::string(64, 'a'), out));
  std::string thirtyTwoEAcute;
  thirtyTwoEAcute.reserve(64);
  for (int i = 0; i < 32; ++i) {
    thirtyTwoEAcute += "\xC3\xA9";
  }
  CHECK_FALSE(OpdsDownloadFolder::normalize(thirtyTwoEAcute, out));
}

TEST_CASE("effective inherits the default when override is empty") {
  CHECK(std::string(OpdsDownloadFolder::effective("", "/")) == "/");
  CHECK(std::string(OpdsDownloadFolder::effective("/", "/")) == "/");
  CHECK(std::string(OpdsDownloadFolder::effective("/Books", "/")) == "/Books");
}

TEST_CASE("opds.json missing downloadFolder leaves an empty override") {
  resetOpdsStore();
  REQUIRE(
      Storage.writeFile("/.crosspoint/opds.json",
                        R"({"servers":[{"name":"A","url":"http://example.com/opds","username":"u","password":"p"}]})"));
  REQUIRE(OPDS_STORE.loadFromFile());
  REQUIRE(OPDS_STORE.getCount() == 1);
  CHECK(OPDS_STORE.getServer(0)->downloadFolder.empty());
}

TEST_CASE("opds.json downloadFolder round-trips and collapses slashes") {
  resetOpdsStore();
  REQUIRE(Storage.writeFile(
      "/.crosspoint/opds.json",
      R"({"servers":[{"name":"A","url":"http://example.com/opds","username":"u","password":"p","downloadFolder":" /Books//Calibre/ "}]})"));
  REQUIRE(OPDS_STORE.loadFromFile());
  REQUIRE(OPDS_STORE.getCount() == 1);
  CHECK(OPDS_STORE.getServer(0)->downloadFolder == "/Books/Calibre");
}

TEST_CASE("invalid downloadFolder fails load without keeping a partial list") {
  resetOpdsStore();
  REQUIRE(Storage.writeFile(
      "/.crosspoint/opds.json",
      R"({"servers":[{"name":"Good","url":"http://example.com/opds","username":"u","password":"p"},{"name":"Bad","url":"http://b.example/opds","username":"u","password":"p","downloadFolder":".."}]})"));
  CHECK_FALSE(OPDS_STORE.loadFromFile());
  CHECK(OPDS_STORE.getCount() == 0);
}

TEST_CASE("addServer and updateServer reject traversal and leave RAM unchanged") {
  resetOpdsStore();
  REQUIRE(OPDS_STORE.addServer({"Alpha", "http://a.example/opds", "a", "secret"}));
  const size_t before = OPDS_STORE.getCount();
  OpdsServer evil;
  evil.name = "Evil";
  evil.url = "http://e.example/opds";
  evil.username = "e";
  evil.password = "pw";
  evil.downloadFolder = "..";
  CHECK_FALSE(OPDS_STORE.addServer(evil));
  CHECK(OPDS_STORE.getCount() == before);

  OpdsServer updated = *OPDS_STORE.getServer(0);
  updated.downloadFolder = "/Books/../x";
  CHECK_FALSE(OPDS_STORE.updateServer(0, updated));
  CHECK(OPDS_STORE.getServer(0)->downloadFolder.empty());
}
