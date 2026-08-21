#include <string>

#include "doctest/doctest.h"
#include "src/util/WifiCredentialStore.h"

namespace {

// Mirrors the shape of the hardware codec (obfuscate/deobfuscate) without the
// MAC-keyed XOR and base64, which need esp_efuse_mac and mbedtls.
std::string passthroughEncode(const std::string& plaintext) { return plaintext; }

std::string passthroughDecode(const char* encoded, bool* ok) {
  const bool present = encoded != nullptr && encoded[0] != '\0';
  if (ok) {
    *ok = present;
  }
  return present ? std::string(encoded) : std::string();
}

constexpr WifiPasswordCodec PLAINTEXT_CODEC{&passthroughEncode, &passthroughDecode};

std::string quoted(const std::string& value) { return "\"" + value + "\""; }

std::string currentFormatEntry(const std::string& ssid, const std::string& password, const size_t declaredLength,
                               const uint32_t declaredCrc) {
  return "{\"ssid\":" + quoted(ssid) + ",\"password_obf\":" + quoted(password) +
         ",\"password_len\":" + std::to_string(declaredLength) + ",\"password_crc32\":" + std::to_string(declaredCrc) +
         "}";
}

std::string document(const std::string& entries, const std::string& lastConnectedSsid = "") {
  return "{\"lastConnectedSsid\":" + quoted(lastConnectedSsid) + ",\"credentials\":[" + entries + "]}";
}

WifiCredentialSnapshot parseOrFail(const std::string& json, bool* needsResave) {
  WifiCredentialSnapshot snapshot;
  REQUIRE(wifi_credentials::parse(json.c_str(), PLAINTEXT_CODEC, snapshot, needsResave));
  return snapshot;
}

}  // namespace

TEST_CASE("legacy plaintext credentials survive an upgrade") {
  const std::string legacy = R"({"lastConnectedSsid":"HomeNet","credentials":[)"
                             R"({"ssid":"HomeNet","password":"hunter2secret"},)"
                             R"({"ssid":"OpenCafe"}]})";

  bool needsResave = false;
  const WifiCredentialSnapshot snapshot = parseOrFail(legacy, &needsResave);

  REQUIRE(snapshot.credentials.size() == 2);
  CHECK(snapshot.credentials[0].ssid == "HomeNet");
  CHECK(snapshot.credentials[0].password == "hunter2secret");
  CHECK(snapshot.credentials[1].ssid == "OpenCafe");
  CHECK(snapshot.credentials[1].password.empty());
  CHECK(snapshot.lastConnectedSsid == "HomeNet");
  CHECK(needsResave);
}

TEST_CASE("legacy obfuscated credentials without integrity fields still load") {
  const std::string legacy = document(R"({"ssid":"HomeNet","password_obf":"hunter2secret"})", "HomeNet");

  bool needsResave = false;
  const WifiCredentialSnapshot snapshot = parseOrFail(legacy, &needsResave);

  REQUIRE(snapshot.credentials.size() == 1);
  CHECK(snapshot.credentials[0].password == "hunter2secret");
  CHECK(needsResave);
}

TEST_CASE("a migrated legacy file rewrites into the current format and reloads clean") {
  const std::string legacy = document(R"({"ssid":"HomeNet","password":"hunter2secret"})", "HomeNet");

  bool needsResave = false;
  const WifiCredentialSnapshot migrated = parseOrFail(legacy, &needsResave);
  REQUIRE(needsResave);

  const std::string rewritten = wifi_credentials::serialize(migrated, PLAINTEXT_CODEC);
  CHECK(rewritten.find("password_len") != std::string::npos);
  CHECK(rewritten.find("password_crc32") != std::string::npos);

  bool reloadNeedsResave = true;
  const WifiCredentialSnapshot reloaded = parseOrFail(rewritten, &reloadNeedsResave);

  REQUIRE(reloaded.credentials.size() == 1);
  CHECK(reloaded.credentials[0].ssid == "HomeNet");
  CHECK(reloaded.credentials[0].password == "hunter2secret");
  CHECK(reloaded.lastConnectedSsid == "HomeNet");
  CHECK_FALSE(reloadNeedsResave);
}

TEST_CASE("an open network round-trips without being flagged for rewrite") {
  WifiCredentialSnapshot original;
  original.credentials.push_back({"OpenCafe", ""});
  original.lastConnectedSsid = "OpenCafe";

  bool needsResave = true;
  const WifiCredentialSnapshot reloaded =
      parseOrFail(wifi_credentials::serialize(original, PLAINTEXT_CODEC), &needsResave);

  REQUIRE(reloaded.credentials.size() == 1);
  CHECK(reloaded.credentials[0].ssid == "OpenCafe");
  CHECK(reloaded.credentials[0].password.empty());
  CHECK_FALSE(needsResave);
}

TEST_CASE("a checksum mismatch discards the entry instead of trying the wrong password") {
  const std::string password = "hunter2secret";
  const std::string corrupted =
      document(currentFormatEntry("HomeNet", password, password.size(), credential_integrity::crc32("different")) +
                   "," + currentFormatEntry("Backup", "goodpassword", 12, credential_integrity::crc32("goodpassword")),
               "HomeNet");

  bool needsResave = false;
  const WifiCredentialSnapshot snapshot = parseOrFail(corrupted, &needsResave);

  REQUIRE(snapshot.credentials.size() == 1);
  CHECK(snapshot.credentials[0].ssid == "Backup");
  // A corrupt entry is dropped from memory but the file is deliberately NOT
  // rewritten: persisting it minus the damaged network would turn one flipped
  // bit into permanent credential loss. Migration resaves; corruption does not.
  CHECK_FALSE(needsResave);
}

TEST_CASE("a length mismatch discards the entry") {
  const std::string password = "hunter2secret";
  const std::string truncated =
      document(currentFormatEntry("HomeNet", password, password.size() - 1, credential_integrity::crc32(password)));

  bool needsResave = false;
  const WifiCredentialSnapshot snapshot = parseOrFail(truncated, &needsResave);

  CHECK(snapshot.credentials.empty());
  // A corrupt entry is dropped from memory but the file is deliberately NOT
  // rewritten: persisting it minus the damaged network would turn one flipped
  // bit into permanent credential loss. Migration resaves; corruption does not.
  CHECK_FALSE(needsResave);
}

TEST_CASE("a non-numeric length or checksum discards the entry") {
  bool needsResave = false;
  const WifiCredentialSnapshot badLength =
      parseOrFail(document(R"({"ssid":"HomeNet","password_obf":"pw","password_len":"eight"})"), &needsResave);
  CHECK(badLength.credentials.empty());
  // A corrupt entry is dropped from memory but the file is deliberately NOT
  // rewritten: persisting it minus the damaged network would turn one flipped
  // bit into permanent credential loss. Migration resaves; corruption does not.
  CHECK_FALSE(needsResave);

  needsResave = false;
  const WifiCredentialSnapshot badCrc = parseOrFail(
      document(R"({"ssid":"HomeNet","password_obf":"pw","password_len":2,"password_crc32":"nope"})"), &needsResave);
  CHECK(badCrc.credentials.empty());
  // A corrupt entry is dropped from memory but the file is deliberately NOT
  // rewritten: persisting it minus the damaged network would turn one flipped
  // bit into permanent credential loss. Migration resaves; corruption does not.
  CHECK_FALSE(needsResave);
}

TEST_CASE("passwords longer than the 64 byte bound are rejected on load") {
  const std::string tooLong(WifiCredentialStore::MAX_PASSWORD_LENGTH + 1, 'x');

  SUBCASE("declared length is rejected before the value is decoded") {
    bool needsResave = false;
    const WifiCredentialSnapshot snapshot = parseOrFail(
        document(currentFormatEntry("HomeNet", tooLong, tooLong.size(), credential_integrity::crc32(tooLong))),
        &needsResave);
    CHECK(snapshot.credentials.empty());
    // Oversized is corruption, not a format to migrate: the file is left
    // untouched so the entry stays recoverable.
    CHECK_FALSE(needsResave);
  }

  SUBCASE("an undeclared oversized legacy value is rejected too") {
    bool needsResave = false;
    const WifiCredentialSnapshot snapshot =
        parseOrFail(document(R"({"ssid":"HomeNet","password":")" + tooLong + R"("})"), &needsResave);
    CHECK(snapshot.credentials.empty());
    // Oversized is corruption, not a format to migrate: the file is left
    // untouched so the entry stays recoverable.
    CHECK_FALSE(needsResave);
  }

  SUBCASE("a password of exactly 64 bytes is accepted") {
    const std::string atLimit(WifiCredentialStore::MAX_PASSWORD_LENGTH, 'x');
    bool needsResave = false;
    const WifiCredentialSnapshot snapshot = parseOrFail(
        document(currentFormatEntry("HomeNet", atLimit, atLimit.size(), credential_integrity::crc32(atLimit))),
        &needsResave);
    REQUIRE(snapshot.credentials.size() == 1);
    CHECK(snapshot.credentials[0].password == atLimit);
    CHECK_FALSE(needsResave);
  }
}

TEST_CASE("addCredential enforces the 64 byte bound and the network cap") {
  WIFI_STORE.clearAll();

  CHECK_FALSE(WIFI_STORE.addCredential("HomeNet", std::string(WifiCredentialStore::MAX_PASSWORD_LENGTH + 1, 'x')));
  CHECK(WIFI_STORE.getCredentials().empty());

  CHECK(WIFI_STORE.addCredential("HomeNet", std::string(WifiCredentialStore::MAX_PASSWORD_LENGTH, 'x')));
  CHECK_FALSE(WIFI_STORE.addCredential("", "whatever"));

  for (size_t i = 1; i < WifiCredentialStore::MAX_NETWORKS; ++i) {
    CHECK(WIFI_STORE.addCredential("Net" + std::to_string(i), "password"));
  }
  CHECK(WIFI_STORE.getCredentials().size() == WifiCredentialStore::MAX_NETWORKS);
  CHECK_FALSE(WIFI_STORE.addCredential("OneTooMany", "password"));

  WIFI_STORE.clearAll();
}

TEST_CASE("a file holding more than the cap loads only the cap") {
  std::string entries;
  for (size_t i = 0; i < WifiCredentialStore::MAX_NETWORKS + 3; ++i) {
    if (i > 0) {
      entries += ",";
    }
    entries += currentFormatEntry("Net" + std::to_string(i), "password", 8, credential_integrity::crc32("password"));
  }

  bool needsResave = false;
  const WifiCredentialSnapshot snapshot = parseOrFail(document(entries), &needsResave);
  CHECK(snapshot.credentials.size() == WifiCredentialStore::MAX_NETWORKS);
}

TEST_CASE("readers get a detached copy rather than a view into the shared vector") {
  WIFI_STORE.clearAll();
  REQUIRE(WIFI_STORE.addCredential("HomeNet", "hunter2secret"));

  const std::vector<WifiCredential> before = WIFI_STORE.getCredentials();
  const std::optional<WifiCredential> found = WIFI_STORE.findCredential("HomeNet");
  REQUIRE(found.has_value());
  CHECK(found->password == "hunter2secret");

  REQUIRE(WIFI_STORE.removeCredential("HomeNet"));

  CHECK(before.size() == 1);
  CHECK(before[0].ssid == "HomeNet");
  CHECK(found->ssid == "HomeNet");
  CHECK_FALSE(WIFI_STORE.findCredential("HomeNet").has_value());
  CHECK_FALSE(WIFI_STORE.findCredential("").has_value());

  WIFI_STORE.clearAll();
}

TEST_CASE("summaries and log descriptions carry no password material") {
  const std::string password = "hunter2secret";

  WIFI_STORE.clearAll();
  REQUIRE(WIFI_STORE.addCredential("HomeNet", password));
  REQUIRE(WIFI_STORE.addCredential("OpenCafe", ""));
  WIFI_STORE.setLastConnectedSsid("HomeNet");

  const std::vector<WifiCredentialSummary> summaries = WIFI_STORE.getCredentialSummaries();
  REQUIRE(summaries.size() == 2);
  CHECK(summaries[0].ssid == "HomeNet");
  CHECK(summaries[0].hasPassword);
  CHECK(summaries[0].isLastConnected);
  CHECK(summaries[1].ssid == "OpenCafe");
  CHECK_FALSE(summaries[1].hasPassword);
  CHECK_FALSE(summaries[1].isLastConnected);

  const std::string described = wifi_credentials::describe(WIFI_STORE.snapshot());
  CHECK(described.find(password) == std::string::npos);
  CHECK(described.find("HomeNet") != std::string::npos);
  CHECK(described.find("[secured]") != std::string::npos);
  CHECK(described.find("[open]") != std::string::npos);
  CHECK(described.find("[last]") != std::string::npos);

  WIFI_STORE.clearAll();
}

TEST_CASE("a malformed document is rejected without touching the store") {
  WIFI_STORE.clearAll();
  REQUIRE(WIFI_STORE.addCredential("HomeNet", "hunter2secret"));

  WifiCredentialSnapshot snapshot;
  bool needsResave = false;
  CHECK_FALSE(wifi_credentials::parse("{not json", PLAINTEXT_CODEC, snapshot, &needsResave));
  CHECK(WIFI_STORE.getCredentials().size() == 1);

  WIFI_STORE.clearAll();
}
