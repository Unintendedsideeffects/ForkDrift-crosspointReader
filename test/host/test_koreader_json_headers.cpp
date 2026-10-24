#include "KOReaderCredentialStore.h"
#include "KOReaderJsonIO.h"
#include "KOReaderProgressPolicy.h"
#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"

namespace {
constexpr const char* kPath = "/.crosspoint/koreader.json";

void resetStore() {
  for (size_t i = 0; i < KOReaderCredentialStore::PROFILE_COUNT; ++i) {
    KOREADER_STORE.setActiveProfileIndex(i);
    KOREADER_STORE.setCredentials("", "");
    KOREADER_STORE.setServerUrl("");
    KOREADER_STORE.setProfileName("");
    KOREADER_STORE.setCustomHeader(0, "", "");
    KOREADER_STORE.setCustomHeader(1, "", "");
  }
  KOREADER_STORE.setActiveProfileIndex(0);
  KOREADER_STORE.setMatchMethod(DocumentMatchMethod::FILENAME);
}
}  // namespace

TEST_CASE("legacy koreader.json without headers or profiles loads into profile 0") {
  resetStore();
  Storage.reset();
  Storage.mkdir("/.crosspoint");
  REQUIRE(Storage.writeFile(
      kPath, R"({"username":"alice","password":"secret","serverUrl":"https://sync.example","matchMethod":1})"));

  bool needsResave = false;
  REQUIRE(KOReaderJsonIO::load(KOREADER_STORE, Storage.readFile(kPath).c_str(), &needsResave));
  CHECK(needsResave);
  CHECK(KOREADER_STORE.getUsername() == "alice");
  CHECK(KOREADER_STORE.getPassword() == "secret");
  CHECK(KOREADER_STORE.getServerUrl() == "https://sync.example");
  CHECK(KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::BINARY);
  CHECK(KOREADER_STORE.getActiveProfileIndex() == 0);
  CHECK(KOREADER_STORE.getCustomHeaders()[0].name.empty());
  CHECK(KOREADER_STORE.getCustomHeaders()[1].name.empty());
}

TEST_CASE("koreader.json headers round-trip through value_obf") {
  resetStore();
  Storage.reset();
  Storage.mkdir("/.crosspoint");
  KOREADER_STORE.setActiveProfileIndex(0);
  KOREADER_STORE.setCredentials("bob", "pw");
  KOREADER_STORE.setServerUrl("https://booklore.example/api/koreader");
  REQUIRE(KOREADER_STORE.setCustomHeader(0, "CF-Access-Client-Id", "id-token"));
  REQUIRE(KOREADER_STORE.setCustomHeader(1, "X-Proxy-Token", "proxy-secret"));
  REQUIRE(KOREADER_STORE.saveToFile());

  const String json = Storage.readFile(kPath);
  CHECK(json.indexOf("value_obf") >= 0);
  CHECK(json.indexOf("proxy-secret") >= 0);
  CHECK(json.indexOf("profiles") >= 0);

  KOREADER_STORE.setCredentials("", "");
  KOREADER_STORE.setCustomHeader(0, "", "");
  KOREADER_STORE.setCustomHeader(1, "", "");
  bool needsResave = true;
  REQUIRE(KOReaderJsonIO::load(KOREADER_STORE, json.c_str(), &needsResave));
  CHECK(KOREADER_STORE.getCustomHeaders()[0].name == "CF-Access-Client-Id");
  CHECK(KOREADER_STORE.getCustomHeaders()[0].value == "id-token");
  CHECK(KOREADER_STORE.getCustomHeaders()[1].name == "X-Proxy-Token");
  CHECK(KOREADER_STORE.getCustomHeaders()[1].value == "proxy-secret");
}

TEST_CASE("invalid stored header is dropped") {
  resetStore();
  Storage.reset();
  const char* json =
      R"({"username":"a","password_obf":"p","profiles":[{"username":"a","password_obf":"p","headers":[{"name":"Authorization","value_obf":"Basic abc"},{"name":"X-Ok","value_obf":"yes"}]}]})";
  bool needsResave = false;
  REQUIRE(KOReaderJsonIO::load(KOREADER_STORE, json, &needsResave));
  CHECK(KOREADER_STORE.getCustomHeaders()[0].name.empty());
  CHECK(KOREADER_STORE.getCustomHeaders()[1].name == "X-Ok");
  CHECK(KOREADER_STORE.getCustomHeaders()[1].value == "yes");
}

TEST_CASE("switching profiles isolates credentials") {
  resetStore();
  Storage.reset();
  KOREADER_STORE.setActiveProfileIndex(0);
  KOREADER_STORE.setCredentials("one", "pw1");
  KOREADER_STORE.setProfileName("Booklore");
  REQUIRE(KOREADER_STORE.setActiveProfileIndex(1));
  KOREADER_STORE.setCredentials("two", "pw2");
  KOREADER_STORE.setProfileName("Official");
  REQUIRE(KOREADER_STORE.setActiveProfileIndex(0));
  CHECK(KOREADER_STORE.getUsername() == "one");
  CHECK(KOREADER_STORE.getProfileName() == "Booklore");
  REQUIRE(KOREADER_STORE.setActiveProfileIndex(1));
  CHECK(KOREADER_STORE.getUsername() == "two");
  CHECK(KOREADER_STORE.getProfileName() == "Official");
}

TEST_CASE("percentage-only progress is accepted in unit interval") {
  CHECK(koSyncHasStoredProgress(true, true, 0.f));
  CHECK(koSyncHasStoredProgress(true, true, 1.f));
  CHECK(koSyncHasStoredProgress(true, true, 0.42f));
  CHECK_FALSE(koSyncHasStoredProgress(true, true, 1.5f));
  CHECK_FALSE(koSyncHasStoredProgress(true, false, 0.f));
  CHECK(koSyncHasStoredProgress(false, false, 0.f));
  CHECK_FALSE(koSyncPercentageUsable(true, 2.f));
}
