#include <string>

#include "OpdsServerStore.h"
#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"

namespace {

constexpr const char* kOpdsPath = "/.crosspoint/opds.json";

void writeOpdsFile(const char* name, const char* url = "http://example.com/opds") {
  const std::string json = std::string("{\"servers\":[{\"name\":\"") + name + "\",\"url\":\"" + url +
                           "\",\"username\":\"user\",\"password\":\"pass\"}]}";
  Storage.mkdir("/.crosspoint");
  REQUIRE(Storage.writeFile(kOpdsPath, json.c_str()));
}

}  // namespace

TEST_CASE("opds server store ensureLoaded reload policy") {
  Storage.reset();
  OpdsServerStore& store = OPDS_STORE;

  SUBCASE("markDirty ensureLoaded reloads from disk") {
    writeOpdsFile("Alpha");
    REQUIRE(store.loadFromFile());
    CHECK(Storage.openFileForReadCount() == 1);
    REQUIRE(store.getCount() == 1);
    CHECK(store.getServer(0)->name == "Alpha");

    writeOpdsFile("Beta");
    store.markDirty();
    store.ensureLoaded();
    CHECK(Storage.openFileForReadCount() == 2);
    REQUIRE(store.getCount() == 1);
    CHECK(store.getServer(0)->name == "Beta");
  }

  SUBCASE("clean ensureLoaded does not re-read") {
    writeOpdsFile("Alpha");
    REQUIRE(store.loadFromFile());
    CHECK(Storage.openFileForReadCount() == 1);
    REQUIRE(store.getCount() == 1);
    CHECK(store.getServer(0)->name == "Alpha");

    writeOpdsFile("Beta");
    store.ensureLoaded();
    CHECK(Storage.openFileForReadCount() == 1);
    REQUIRE(store.getCount() == 1);
    CHECK(store.getServer(0)->name == "Alpha");
  }

  SUBCASE("addServer marks dirty so ensureLoaded reloads") {
    writeOpdsFile("Alpha");
    REQUIRE(store.loadFromFile());
    CHECK(Storage.openFileForReadCount() == 1);

    const OpdsServer added{"Gamma", "http://gamma.example/opds", "gamma", "secret"};
    REQUIRE(store.addServer(added));
    CHECK(store.getCount() == 2);

    writeOpdsFile("External");
    store.ensureLoaded();
    CHECK(Storage.openFileForReadCount() == 2);
    REQUIRE(store.getCount() == 1);
    CHECK(store.getServer(0)->name == "External");
  }

  SUBCASE("headers value_obf round-trip and missing headers stay empty") {
    writeOpdsFile("Alpha");
    REQUIRE(store.loadFromFile());
    REQUIRE(store.getCount() == 1);
    CHECK(store.getServer(0)->customHeaders[0].name.empty());

    OpdsServer withHeader = *store.getServer(0);
    withHeader.customHeaders[0] = {"CF-Access-Client-Id", "token"};
    REQUIRE(store.updateServer(0, withHeader));
    const String json = Storage.readFile(kOpdsPath);
    CHECK(json.indexOf("value_obf") >= 0);
    CHECK(json.indexOf("CF-Access-Client-Id") >= 0);

    store.markDirty();
    REQUIRE(store.loadFromFile());
    REQUIRE(store.getCount() == 1);
    CHECK(store.getServer(0)->customHeaders[0].name == "CF-Access-Client-Id");
    CHECK(store.getServer(0)->customHeaders[0].value == "token");
  }
}
