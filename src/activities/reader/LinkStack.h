#pragma once

#include <HalStorage.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

/*
 * Versioned consume-once sidecar `.crosspoint/epub_<hash>/links.bin`.
 * Layout: u8 version, u8 depth (1..kMaxDepth), then depth × (u16 LE spine, u16 LE page).
 * Missing, oversized, corrupt, or out-of-range spine indices are ignored.
 */
struct LinkPosition {
  int spineIndex = 0;
  int pageNumber = 0;
};

struct LinkStack {
  static constexpr uint8_t kFileVersion = 1;
  static constexpr int kMaxDepth = 3;
  static constexpr size_t kMaxBytes = 2 + static_cast<size_t>(kMaxDepth) * 4;

  LinkPosition entries[kMaxDepth] = {};
  int depth = 0;

  void push(int spineIndex, int pageNumber) {
    if (depth == kMaxDepth) {
      std::copy(entries + 1, entries + kMaxDepth, entries);
      depth--;
    }
    entries[depth++] = {spineIndex, pageNumber};
  }

  size_t encode(uint8_t* out, size_t cap) const {
    if (!out || depth < 1 || depth > kMaxDepth) {
      return 0;
    }
    const size_t size = 2 + static_cast<size_t>(depth) * 4;
    if (size > kMaxBytes || cap < size) {
      return 0;
    }
    out[0] = kFileVersion;
    out[1] = static_cast<uint8_t>(depth);
    for (int i = 0; i < depth; i++) {
      uint8_t* p = out + 2 + static_cast<size_t>(i) * 4;
      p[0] = static_cast<uint8_t>(entries[i].spineIndex & 0xFF);
      p[1] = static_cast<uint8_t>((entries[i].spineIndex >> 8) & 0xFF);
      p[2] = static_cast<uint8_t>(entries[i].pageNumber & 0xFF);
      p[3] = static_cast<uint8_t>((entries[i].pageNumber >> 8) & 0xFF);
    }
    return size;
  }

  static bool decode(const uint8_t* data, size_t size, int spineCount, LinkStack& out) {
    out = {};
    if (!data || size < 2 || size > kMaxBytes) {
      return false;
    }
    if (data[0] != kFileVersion) {
      return false;
    }
    const int depth = data[1];
    if (depth < 1 || depth > kMaxDepth) {
      return false;
    }
    if (size != 2 + static_cast<size_t>(depth) * 4) {
      return false;
    }
    for (int i = 0; i < depth; i++) {
      const uint8_t* p = data + 2 + static_cast<size_t>(i) * 4;
      const int spine = p[0] | (p[1] << 8);
      const int page = p[2] | (p[3] << 8);
      if (spine >= spineCount) {
        out = {};
        return false;
      }
      out.entries[i] = {spine, page};
    }
    out.depth = depth;
    return true;
  }
};

inline const char* kLinkStackFileName = "links.bin";

inline std::string linkStackPath(const std::string& cachePath) { return cachePath + "/" + kLinkStackFileName; }

inline bool saveLinkStackFile(const std::string& cachePath, const LinkStack& stack) {
  uint8_t data[LinkStack::kMaxBytes];
  const size_t size = stack.encode(data, sizeof(data));
  if (size == 0) {
    return false;
  }
  HalFile f;
  if (!Storage.openFileForWrite("ERS", linkStackPath(cachePath), f)) {
    return false;
  }
  return f.write(data, size) == size;
}

inline bool loadLinkStackFile(const std::string& cachePath, int spineCount, LinkStack& stack) {
  stack = {};
  const std::string path = linkStackPath(cachePath);
  if (!Storage.exists(path.c_str())) {
    return false;
  }
  bool applied = false;
  {
    HalFile f;
    uint8_t data[LinkStack::kMaxBytes + 1];
    if (Storage.openFileForRead("ERS", path, f)) {
      const size_t n = f.read(data, sizeof(data));
      if (n <= LinkStack::kMaxBytes) {
        applied = LinkStack::decode(data, n, spineCount, stack);
      }
    }
  }
  Storage.remove(path.c_str());
  if (!applied) {
    stack = {};
  }
  return applied;
}

inline void discardLinkStackFile(const std::string& cachePath) { Storage.remove(linkStackPath(cachePath).c_str()); }
