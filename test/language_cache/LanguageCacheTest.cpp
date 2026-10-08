#include <LanguageCache.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace language_cache;
#define CHECK(...)                                                   \
  do {                                                               \
    if (!(__VA_ARGS__)) {                                            \
      std::fprintf(stderr, "line %d: %s\n", __LINE__, #__VA_ARGS__); \
      std::exit(1);                                                  \
    }                                                                \
  } while (0)

namespace {
std::vector<Key> keys;
std::vector<std::string> englishText;
Key at(size_t i) { return keys[i]; }
const char* english(uint16_t i) { return englishText[i].c_str(); }
void schemaInit(bool changed = false) {
  englishText = changed ? std::vector<std::string>{"Added", "Home", "Changed menu", "%u min"}
                        : std::vector<std::string>{"Home", "Menu", "%u min"};
  keys = changed ? std::vector<Key>{{hash("STR_NEW"), hash("Added"), "STR_NEW", 0, false},
                                    {hash("STR_HOME"), hash("Home"), "STR_HOME", 1, false},
                                    {hash("STR_MENU"), hash("Changed menu"), "STR_MENU", 2, false},
                                    {hash("STR_TIME_FORMAT"), hash("%u min"), "STR_TIME_FORMAT", 3, true}}
                 : std::vector<Key>{{hash("STR_HOME"), hash("Home"), "STR_HOME", 0, false},
                                    {hash("STR_MENU"), hash("Menu"), "STR_MENU", 1, false},
                                    {hash("STR_TIME_FORMAT"), hash("%u min"), "STR_TIME_FORMAT", 2, true}};
  std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.hash < b.hash; });
}
Schema schema() { return {keys.size(), at, english}; }
struct Source {
  std::string bytes;
  size_t pos = 0;
  int failAt = -1;
  static int read(void* ctx, void* dest, size_t n) {
    auto& s = *static_cast<Source*>(ctx);
    if (s.failAt >= 0 && s.pos >= static_cast<size_t>(s.failAt)) return -1;
    n = std::min(n, s.bytes.size() - s.pos);
    std::memcpy(dest, s.bytes.data() + s.pos, n);
    s.pos += n;
    return static_cast<int>(n);
  }
  Input input() {
    pos = 0;
    return {this, read};
  }
};
struct Storage {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(3 * SLOT_SIZE, 0xff);
  int failMutation = -1;
  int mutations = 0;
  bool torn = false;
  static bool read(void* ctx, size_t off, void* dest, size_t n) {
    auto& s = *static_cast<Storage*>(ctx);
    if (off > s.bytes.size() || n > s.bytes.size() - off) return false;
    std::memcpy(dest, s.bytes.data() + off, n);
    return true;
  }
  static bool write(void* ctx, size_t off, const void* data, size_t n) {
    auto& s = *static_cast<Storage*>(ctx);
    if (off > s.bytes.size() || n > s.bytes.size() - off) return false;
    const bool fail = s.mutations++ == s.failMutation;
    if (fail && !s.torn) return false;
    const auto* source = static_cast<const uint8_t*>(data);
    const size_t count = fail ? n / 2 : n;
    for (size_t i = 0; i < count; ++i) {
      CHECK((s.bytes[off + i] & source[i]) == source[i]);
      s.bytes[off + i] &= source[i];
    }
    return !fail;
  }
  static bool erase(void* ctx, size_t off, size_t n) {
    auto& s = *static_cast<Storage*>(ctx);
    CHECK(off >= SLOT_SIZE && off % 4096 == 0 && n % 4096 == 0);
    if (off > s.bytes.size() || n > s.bytes.size() - off) return false;
    const bool fail = s.mutations++ == s.failMutation;
    if (fail && !s.torn) return false;
    std::fill_n(s.bytes.data() + off, fail ? n / 2 : n, 0xff);
    return !fail;
  }
  Flash flash() { return {this, bytes.size(), read, write, erase}; }
  uint8_t* slot(int index) { return bytes.data() + slotOffset(bytes.size(), index); }
};
std::string document(
    std::string body = "STR_HOME: \"Accueil\"\nSTR_MENU: \"Réglages\"\nSTR_TIME_FORMAT: \"%u minutes\"\n",
    const char* code = "FR") {
  return std::string("# Community translation\n_language_code: \"") + code + "\"\n_language_name: \"Français\"\n\n" +
         body;
}
Result put(Storage& storage, const std::string& text, Installed& result, int pinned = -1) {
  Source source{text};
  return install(source.input(), schema(), storage.flash(), pinned, result);
}
void basics() {
  schemaInit();
  Storage storage;
  Installed info;
  CHECK(put(storage, document(), info) == Result::Ok);
  CHECK(info.slot == 0 && info.generation == 1 && std::strcmp(info.metadata.code, "FR") == 0);
  uint16_t offsets[4];
  Installed opened;
  CHECK(open(storage.slot(0), SLOT_SIZE, schema(), opened, offsets));
  CHECK(std::strcmp(reinterpret_cast<char*>(storage.slot(0) + offsets[0]), "Accueil") == 0);
  CHECK(std::strcmp(reinterpret_cast<char*>(storage.slot(0) + offsets[2]), "%u minutes") == 0);
  CHECK(opened.checksum == info.checksum);
  // IDs change, a key is added, and English text changes. Compatible entries survive.
  schemaInit(true);
  CHECK(open(storage.slot(0), SLOT_SIZE, schema(), opened, offsets));
  CHECK(offsets[0] == MISSING && offsets[2] == MISSING);
  CHECK(std::strcmp(reinterpret_cast<char*>(storage.slot(0) + offsets[1]), "Accueil") == 0);
  CHECK(std::strcmp(reinterpret_cast<char*>(storage.slot(0) + offsets[3]), "%u minutes") == 0);
  schemaInit();
  // Applying a replacement never touches the mapped/pinned original.
  const auto old = storage.bytes;
  CHECK(put(storage, document("STR_HOME: \"Bonjour\"\n"), info, 0) == Result::Ok);
  CHECK(info.slot == 1 && info.generation == 2);
  CHECK(std::memcmp(storage.slot(0), old.data() + SLOT_SIZE, SLOT_SIZE) == 0);
  CHECK(open(storage.slot(1), SLOT_SIZE, schema(), opened, offsets));
  CHECK(offsets[1] == MISSING && offsets[2] == MISSING);
  // Simulate the next reboot pinning slot 1, then switch back.
  CHECK(put(storage, document(), info, 1) == Result::Ok);
  CHECK(info.slot == 0 && info.generation == 3);
}
void parser() {
  schemaInit();
  const auto result = [](const std::string& text) {
    Storage storage;
    Installed i;
    return put(storage, text, i);
  };
  CHECK(result(document("STR_HOME: \"Bonjour\\n\\\"ami\\\"\\\\fin\" # comment\r\n")) == Result::Ok);
  CHECK(result(document("STR_HOME: \"x\"\nSTR_HOME: \"y\"\n")) == Result::Duplicate);
  CHECK(result(document("STR_UNUSED: \"x\"\nSTR_UNUSED: \"y\"\n")) == Result::Duplicate);
  CHECK(result(document("STR_HOME: bare\n")) == Result::Invalid);
  CHECK(result(document("STR_HOME: \"x\\q\"\n")) == Result::Invalid);
  CHECK(result(document("STR_HOME: \"x\" trailing\n")) == Result::Invalid);
  CHECK(result(document("STR_HOME: \"\xc0\xaf\"\n")) == Result::Invalid);
  CHECK(result(document(std::string("STR_HOME: \"a\0b\"\n", 16))) == Result::Invalid);
  CHECK(result(document("# \xff\n")) == Result::Invalid);
  CHECK(result(document("STR_HOME: \"x\"\n_direction: \"rtl\"\n")) == Result::Invalid);
  CHECK(result("STR_HOME: \"x\"\n") == Result::MetadataMissing);
  CHECK(result(document({}, "EN")) == Result::Invalid);
  CHECK(result(document({}, "../FR")) == Result::Invalid);
  CHECK(result(document("STR_HOME: \"" + std::string(2048, 'x') + "\"\n")) == Result::TooLarge);
  CHECK(result(document("STR_NOT_YET_KNOWN: \"anything\"\n")) == Result::Ok);
  CHECK(result(document("STR_HOME: \"\"\n")) == Result::Ok);
  Source metadata{document("STR_HOME: \"x\"", "ZZ-CUSTOM")};
  Metadata m;
  CHECK(inspect(metadata.input(), m) == Result::Ok && std::strcmp(m.code, "ZZ-CUSTOM") == 0 && !m.rtl);
  metadata.bytes = document({}, "AR");
  CHECK(inspect(metadata.input(), m) == Result::Ok && m.rtl && std::strcmp(m.keyboard, "AR") == 0);
  metadata.bytes = "_language_code: \"foo\"\n_language_name: \"Foo\"\n_direction: \"rtl\"\n_keyboard: \"HE\"\n";
  CHECK(inspect(metadata.input(), m) == Result::Ok && m.rtl && std::strcmp(m.code, "FOO") == 0 &&
        std::strcmp(m.keyboard, "HE") == 0);
}
void formats() {
  CHECK(compatibleFormat("%u min", "%u minutes"));
  CHECK(compatibleFormat("%d %.2f%% %zu %lld %s", "%i %.1f%% %zu %lld %s"));
  CHECK(compatibleFormat("%*.*f", "%*.*f"));
  CHECK(!compatibleFormat("%u", "%s"));
  CHECK(!compatibleFormat("%s %u", "%u %s"));
  CHECK(!compatibleFormat("%u", "%n"));
  CHECK(!compatibleFormat("%u", "%1$u"));
  CHECK(!compatibleFormat("%u", "%lu"));
  CHECK(!compatibleFormat("%f", "%*f"));
  CHECK(!compatibleFormat("%u", "%999999999u"));
  CHECK(!compatibleFormat("%u", "%"));
  Storage storage;
  Installed info;
  uint16_t offsets[3];
  CHECK(put(storage, document("STR_TIME_FORMAT: \"%s\"\nSTR_HOME: \"Accueil\"\n"), info) == Result::Ok);
  CHECK(open(storage.slot(info.slot), SLOT_SIZE, schema(), info, offsets));
  CHECK(offsets[2] == MISSING && offsets[0] != MISSING);
  // A later firmware may start formatting a previously literal key without
  // changing its English text. Recheck that contract when building the index.
  CHECK(put(storage, document("STR_HOME: \"%n\"\n"), info) == Result::Ok);
  for (auto& key : keys)
    if (key.id == 0) key.formatted = true;
  CHECK(open(storage.slot(info.slot), SLOT_SIZE, schema(), info, offsets));
  CHECK(offsets[0] == MISSING);
  schemaInit();
}
void capacity() {
  std::vector<std::string> names;
  names.reserve(100);
  englishText.clear();
  englishText.reserve(100);
  keys.clear();
  keys.reserve(100);
  std::string text = document("");
  for (size_t i = 0; i < 100; ++i) {
    names.push_back("STR_LONG_" + std::to_string(i));
    englishText.push_back("English " + std::to_string(i));
    keys.push_back({hash(names.back().c_str()), hash(englishText.back().c_str()), names.back().c_str(),
                    static_cast<uint16_t>(i), false});
    text += names.back() + ": \"" + std::string(1000, 'x') + "\"\n";
  }
  std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.hash < b.hash; });
  Storage storage;
  Installed info;
  CHECK(put(storage, text, info) == Result::TooLarge);
  CHECK(!open(storage.slot(0), SLOT_SIZE, schema(), info));
  schemaInit();
}
void provisioningAndFailures() {
  schemaInit();
  Storage foreign;
  Installed info;
  // Firmware owns this data region regardless of its previous filesystem.
  // Provisioning changes only the chosen cache slot, even with data elsewhere.
  std::fill(foreign.bytes.begin(), foreign.bytes.end(), 0xa5);
  const auto before = foreign.bytes;
  CHECK(put(foreign, document(), info) == Result::Ok && info.slot == 0 && info.generation == 1);
  CHECK(open(foreign.slot(0), SLOT_SIZE, schema(), info));
  CHECK(std::memcmp(foreign.bytes.data(), before.data(), SLOT_SIZE) == 0);
  CHECK(std::memcmp(foreign.slot(1), before.data() + 2 * SLOT_SIZE, SLOT_SIZE) == 0);
  // A first installation interrupted at any flash mutation can be retried.
  for (bool torn : {false, true})
    for (int failure = 0; failure < foreign.mutations; ++failure) {
      Storage trial;
      trial.bytes = before;
      trial.failMutation = failure;
      trial.torn = torn;
      CHECK(put(trial, document(), info) == Result::Io);
      CHECK(std::memcmp(trial.bytes.data(), before.data(), SLOT_SIZE) == 0);
      CHECK(std::memcmp(trial.slot(1), before.data() + 2 * SLOT_SIZE, SLOT_SIZE) == 0);
      trial.failMutation = -1;
      CHECK(put(trial, document(), info) == Result::Ok);
      CHECK(open(trial.slot(info.slot), SLOT_SIZE, schema(), info));
    }
  // Ownership policy does not relax partition capacity/alignment checks.
  for (size_t size : {SLOT_SIZE, 3 * SLOT_SIZE - 1}) {
    Storage invalid;
    invalid.bytes.resize(size);
    CHECK(put(invalid, document(), info) == Result::StorageUnavailable && invalid.mutations == 0);
  }
  Storage original;
  CHECK(put(original, document(), info) == Result::Ok);
  CHECK(put(original, document("STR_HOME: \"Older inactive copy\"\n"), info, 0) == Result::Ok);
  const auto committed = original.bytes;
  Storage success = original;
  success.mutations = 0;
  CHECK(put(success, document("STR_HOME: \"Replacement\"\n"), info, 0) == Result::Ok);
  for (bool torn : {false, true})
    for (int failure = 0; failure < success.mutations; ++failure) {
      Storage trial = original;
      trial.mutations = 0;
      trial.failMutation = failure;
      trial.torn = torn;
      CHECK(put(trial, document("STR_HOME: \"Replacement\"\n"), info, 0) == Result::Io);
      CHECK(std::memcmp(trial.slot(0), committed.data() + SLOT_SIZE, SLOT_SIZE) == 0);
      CHECK(open(trial.slot(0), SLOT_SIZE, schema(), info));
      trial.failMutation = -1;
      CHECK(put(trial, document(), info, 0) == Result::Ok);
    }
  // First-ever provisioning can be retried after a torn ownership marker.
  Storage torn;
  torn.failMutation = 1;
  torn.torn = true;
  CHECK(put(torn, document(), info) == Result::Io);
  torn.failMutation = -1;
  CHECK(put(torn, document(), info) == Result::Ok);
  // A failed settings save would keep the old generation. Both remain readable.
  CHECK(open(success.slot(0), SLOT_SIZE, schema(), info) && info.generation == 1);
  CHECK(open(success.slot(1), SLOT_SIZE, schema(), info) && info.generation == 3);
  // Corrupt any committed header/body byte; checksum/structure rejects it.
  for (size_t offset :
       {size_t(16), size_t(20), size_t(28), size_t(32), size_t(36), size_t(40), size_t(72), HEADER_SIZE + 18}) {
    Storage corrupt = original;
    corrupt.slot(0)[offset] ^= 1;
    CHECK(!open(corrupt.slot(0), SLOT_SIZE, schema(), info));
  }
  Source broken{document()};
  broken.failAt = 0;
  CHECK(install(broken.input(), schema(), original.flash(), 0, info) == Result::Io);
}
}  // namespace
int main() {
  basics();
  parser();
  formats();
  capacity();
  provisioningAndFailures();
  std::puts("Language cache tests passed: parsing, formats, compatibility, provisioning and interrupted writes");
}
