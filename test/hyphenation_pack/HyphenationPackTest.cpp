#include <HyphenationPack.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace hyphenation_pack;
namespace {
constexpr size_t PARTITION_SIZE = 0x360000;
constexpr size_t UI_BYTES = 2 * 65536;
constexpr size_t CAPACITY = PARTITION_SIZE - UI_BYTES;
void put32(uint8_t* p, uint32_t n) {
  for (size_t i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(n >> (8 * i));
}
bool supported(const char* code, uint8_t prefix, uint8_t suffix) {
  const char* codes[] = {"de", "fr", "ru", "uk", "es", "it", "pt", "sv", "pl"};
  for (const auto* candidate : codes)
    if (!std::strcmp(candidate, code)) return prefix == 2 && suffix == 2;
  return false;
}
struct Device {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(PARTITION_SIZE, 0xff);
  int failAt = -1;
  int failReadOffset = -1;
  int mutations = 0;
  bool corruptWrite = false;
  Device() { std::fill(bytes.begin() + CAPACITY, bytes.end(), 0xa5); }
  bool fail() { return mutations++ == failAt; }
  Flash flash() {
    return {this,
            CAPACITY,
            [](void* p, size_t off, void* out, size_t size) {
              auto& device = *static_cast<Device*>(p);
              assert(off <= CAPACITY && size <= CAPACITY - off);
              if (device.failReadOffset >= 0 && off == static_cast<size_t>(device.failReadOffset)) return false;
              std::memcpy(out, device.bytes.data() + off, size);
              return true;
            },
            [](void* p, size_t off, const void* in, size_t size) {
              auto& device = *static_cast<Device*>(p);
              assert(off <= CAPACITY && size <= CAPACITY - off);
              if (device.fail()) return false;
              const auto* data = static_cast<const uint8_t*>(in);
              for (size_t i = 0; i < size; ++i) {
                assert((device.bytes[off + i] & data[i]) == data[i]);
                device.bytes[off + i] = data[i];
              }
              if (device.corruptWrite && size) device.bytes[off] ^= 1;
              return true;
            },
            [](void* p, size_t off, size_t size) {
              auto& device = *static_cast<Device*>(p);
              assert(off % SECTOR_SIZE == 0 && size % SECTOR_SIZE == 0);
              assert(off <= CAPACITY && size <= CAPACITY - off);
              if (device.fail()) return false;
              std::fill_n(device.bytes.begin() + off, size, 0xff);
              return true;
            },
            nullptr};
  }
  void checkUi() const {
    assert(std::all_of(bytes.begin() + CAPACITY, bytes.end(), [](auto b) { return b == 0xa5; }));
  }
};
Input input(std::vector<uint8_t>& bytes) {
  return {&bytes, bytes.size(), [](void* p, size_t off, void* out, size_t size) {
            auto& data = *static_cast<std::vector<uint8_t>*>(p);
            if (off > data.size() || size > data.size() - off) return false;
            std::memcpy(out, data.data() + off, size);
            return true;
          }};
}
std::vector<uint8_t> pack(const char* code, size_t size = 2301, uint8_t content = 0x42) {
  std::vector<uint8_t> data(PACK_HEADER_SIZE + size, content);
  std::fill_n(data.begin(), PACK_HEADER_SIZE, 0);
  std::memcpy(data.data(), "CPHY\1", 5);
  std::memcpy(data.data() + 5, code, 2);
  data[7] = data[8] = 2;
  put32(data.data() + 12, size - 1);
  put32(data.data() + 16, size);
  put32(data.data() + 20, crc32(data.data() + PACK_HEADER_SIZE, size));
  return data;
}
Store boot(Device& device) {
  Store store;
  assert(store.begin(device.flash(), supported) == Result::Ok);
  return store;
}
void storageTests() {
  assert(crc32("123456789", 9) == 0xcbf43926u);
  assert(crc32("56789", 5, crc32("1234", 4)) == 0xcbf43926u);
  Device device;
  auto store = boot(device);
  assert(store.count() == 0);
  auto german = pack("de");
  assert(store.install(input(german)) == Result::Ok);
  assert(store.needsRestart());
  assert(store.count() == 0);  // publication never mutates the pinned bank
  assert(store.install(input(german)) == Result::RestartRequired);
  store = boot(device);
  Entry first;
  assert(store.find("de", first));
  const auto identityBefore = identity(first);
  device.mutations = 0;
  assert(store.install(input(german)) == Result::Unchanged);
  assert(device.mutations == 0);
  auto french = pack("fr", 1073, 0x11);
  assert(store.install(input(french)) == Result::Ok);
  assert(store.count() == 1);
  store = boot(device);
  assert(store.count() == 2);
  Entry relocated;
  assert(store.find("de", relocated));
  assert(identity(relocated) == identityBefore);
  device.checkUi();

  // Each possible interrupted erase/write must leave the previous bank usable.
  const Device baseline = device;
  Device retry = baseline;
  Store retrying;
  retry.failReadOffset = CAPACITY / 2;
  assert(retrying.begin(retry.flash(), supported) == Result::Io);
  retry.failReadOffset = -1;
  retry.bytes[0] ^= 1;
  retry.bytes[CAPACITY / 2] ^= 1;
  assert(retrying.begin(retry.flash(), supported) == Result::Ok);
  assert(retrying.count() == 0);  // No bank retained from the failed scan.
  auto update = pack("de", 3101, 0x28);
  Device successful = baseline;
  successful.mutations = 0;
  auto updateStore = boot(successful);
  assert(updateStore.install(input(update)) == Result::Ok);
  const int operations = successful.mutations;
  for (int stop = 0; stop < operations; ++stop) {
    Device interrupted = baseline;
    interrupted.mutations = 0;
    interrupted.failAt = stop;
    auto old = boot(interrupted);
    assert(old.install(input(update)) == Result::Io);
    interrupted.failAt = -1;
    auto recovered = boot(interrupted);
    Entry entry;
    assert(recovered.count() == 2 && recovered.find("de", entry));
    assert(identity(entry) == identityBefore);
    interrupted.checkUi();
  }
  // Silent write corruption is caught by readback before a commit is published.
  Device broken = baseline;
  broken.corruptWrite = true;
  auto old = boot(broken);
  assert(old.install(input(update)) == Result::Io);
  assert(boot(broken).find("de", first) && identity(first) == identityBefore);

  auto newest = boot(successful);
  assert(newest.find("de", first));
  assert(identity(first) != identityBefore);
  successful.bytes[newest.bankOffset() + first.offset] ^= 1;
  auto fallback = boot(successful);
  assert(fallback.find("de", first) && identity(first) == identityBefore);

  assert(store.remove("de") == Result::Ok);
  store = boot(device);
  assert(store.count() == 1 && !store.find("de", first) && store.find("fr", first));
  assert(store.remove("fr") == Result::Ok);
  store = boot(device);
  assert(store.count() == 0);
  device.checkUi();

  for (const size_t offset : {size_t{0}, size_t{4}, size_t{9}, size_t{10}, size_t{16}, size_t{20}, size_t{24}}) {
    auto bad = german;
    bad[offset] ^= 0x80;
    assert(store.install(input(bad)) != Result::Ok);
    assert(store.count() == 0);
  }
  auto unsupported = pack("el");
  assert(store.install(input(unsupported)) == Result::Unsupported);
  auto english = pack("en");
  assert(store.install(input(english)) == Result::Unsupported);
  auto tooLarge = pack("de", CAPACITY / 2);
  assert(store.install(input(tooLarge)) == Result::NoSpace);
  auto truncated = german;
  truncated.pop_back();
  assert(store.install(input(truncated)) == Result::Invalid);
  auto flags = german;
  flags[7] = 3;
  assert(store.install(input(flags)) == Result::Unsupported);
  auto rootChanged = german;
  put32(rootChanged.data() + 12, 0);
  assert(store.install(input(german)) == Result::Ok);
  store = boot(device);
  assert(store.install(input(rootChanged)) == Result::Ok);  // metadata changes count even with identical payload
  device.checkUi();
  std::puts("Atomic pack storage, recovery, CRC and UI-slot isolation passed");
}
}  // namespace
int main(int argc, char** argv) {
  storageTests();
  // Exercise release-generated real dictionaries through the production loader.
  Device device;
  for (int i = 1; i < argc; ++i) {
    std::ifstream stream(argv[i], std::ios::binary);
    assert(stream);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), {});
    auto store = boot(device);
    assert(store.install(input(bytes)) == Result::Ok);
    device.checkUi();
  }
  if (argc > 1) assert(boot(device).count() == static_cast<size_t>(argc - 1));
}
