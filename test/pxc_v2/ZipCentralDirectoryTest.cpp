#include <ZipFile.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

struct TestEntry {
  std::string name;
  std::string data;
  uint16_t extraLen;
  uint16_t commentLen;
  uint32_t crc;
};

void putLe16(std::vector<uint8_t>& out, const uint16_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
}

void putLe32(std::vector<uint8_t>& out, const uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>(value >> shift));
}

// Builds a STORED zip. CRCs are arbitrary markers: ZipFile reports them as-is.
std::vector<uint8_t> buildZip(const std::vector<TestEntry>& entries) {
  std::vector<uint8_t> out;
  std::vector<uint32_t> localOffsets;
  for (const auto& entry : entries) {
    localOffsets.push_back(static_cast<uint32_t>(out.size()));
    putLe32(out, 0x04034b50);
    putLe16(out, 10);  // version needed
    putLe16(out, 0);   // flags
    putLe16(out, 0);   // stored
    putLe16(out, 0);   // time
    putLe16(out, 0);   // date
    putLe32(out, entry.crc);
    putLe32(out, static_cast<uint32_t>(entry.data.size()));
    putLe32(out, static_cast<uint32_t>(entry.data.size()));
    putLe16(out, static_cast<uint16_t>(entry.name.size()));
    putLe16(out, 0);  // no local extra
    out.insert(out.end(), entry.name.begin(), entry.name.end());
    out.insert(out.end(), entry.data.begin(), entry.data.end());
  }

  const auto centralOffset = static_cast<uint32_t>(out.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    putLe32(out, 0x02014b50);
    putLe16(out, 20);  // version made by
    putLe16(out, 10);  // version needed
    putLe16(out, 0);   // flags
    putLe16(out, 0);   // stored
    putLe16(out, 0);   // time
    putLe16(out, 0);   // date
    putLe32(out, entry.crc);
    putLe32(out, static_cast<uint32_t>(entry.data.size()));
    putLe32(out, static_cast<uint32_t>(entry.data.size()));
    putLe16(out, static_cast<uint16_t>(entry.name.size()));
    putLe16(out, entry.extraLen);
    putLe16(out, entry.commentLen);
    putLe16(out, 0);  // disk
    putLe16(out, 0);  // internal attributes
    putLe32(out, 0);  // external attributes
    putLe32(out, localOffsets[i]);
    out.insert(out.end(), entry.name.begin(), entry.name.end());
    out.insert(out.end(), entry.extraLen, static_cast<uint8_t>('x'));
    out.insert(out.end(), entry.commentLen, static_cast<uint8_t>('c'));
  }
  const auto centralSize = static_cast<uint32_t>(out.size()) - centralOffset;

  putLe32(out, 0x06054b50);
  putLe16(out, 0);
  putLe16(out, 0);
  putLe16(out, static_cast<uint16_t>(entries.size()));
  putLe16(out, static_cast<uint16_t>(entries.size()));
  putLe32(out, centralSize);
  putLe32(out, centralOffset);
  putLe16(out, 0);
  return out;
}

std::vector<TestEntry> makeEntries() {
  std::vector<TestEntry> entries;
  for (int i = 0; i < 600; ++i) {
    TestEntry entry;
    entry.name = "OEBPS/Text/chapter-" + std::to_string(i) + ".xhtml";
    entry.data = std::string(static_cast<size_t>(i % 37 + 1), static_cast<char>('a' + i % 26));
    // Vary trailing field sizes so entries straddle the cursor's 1 KiB chunks.
    entry.extraLen = static_cast<uint16_t>((i * 7) % 61);
    entry.commentLen = static_cast<uint16_t>(i % 5 == 0 ? 900 + i % 300 : 0);
    entry.crc = 0x10000000u + static_cast<uint32_t>(i);
    entries.push_back(std::move(entry));
  }
  entries.insert(entries.begin() + 250, TestEntry{std::string(255, 'n'), "max-name", 3, 2000, 0xABCDEF01u});
  entries.insert(entries.begin() + 400, TestEntry{std::string(300, 'o'), "oversized-name", 0, 0, 0x0BADF00Du});
  return entries;
}

struct CountingOutput : Print {
  std::string bytes;
  size_t write(uint8_t value) override {
    bytes.push_back(static_cast<char>(value));
    return 1;
  }
};

class ZipCentralDirectory : public testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    entries = makeEntries();
    Storage.put(path, buildZip(entries));
  }

  const std::string path = "book.epub";
  std::vector<TestEntry> entries;
};

}  // namespace

TEST_F(ZipCentralDirectory, FreshLookupsFindEveryStoredName) {
  for (const auto& entry : entries) {
    size_t size = 0;
    const bool found = ZipFile(path).getInflatedFileSize(entry.name.c_str(), &size);
    if (entry.name.size() > 255) {
      EXPECT_FALSE(found) << "oversized names are not addressable";
      continue;
    }
    ASSERT_TRUE(found) << entry.name;
    EXPECT_EQ(size, entry.data.size()) << entry.name;
  }
  size_t size = 0;
  EXPECT_FALSE(ZipFile(path).getInflatedFileSize("OEBPS/missing.xhtml", &size));
}

TEST_F(ZipCentralDirectory, ReusedZipWrapsFromItsCursorAndReadsData) {
  ZipFile zip(path);
  ASSERT_TRUE(zip.open());
  // Out-of-order lookups exercise the resume-from-cursor and wrap-around scan.
  for (const size_t index : {500U, 10U, 599U, 251U, 0U, 401U, 250U}) {
    const auto& entry = entries[index];
    CountingOutput output;
    ASSERT_TRUE(zip.readStoredFileToStream(entry.name.c_str(), output)) << entry.name;
    EXPECT_EQ(output.bytes, entry.data) << entry.name;
  }
  size_t size = 0;
  EXPECT_FALSE(zip.getInflatedFileSize("OEBPS/missing.xhtml", &size));
  EXPECT_TRUE(zip.getInflatedFileSize(entries[3].name.c_str(), &size));
  EXPECT_EQ(size, entries[3].data.size());
  zip.close();
}

TEST_F(ZipCentralDirectory, LoadAllStatsMatchesIndividualLookups) {
  ZipFile zip(path);
  ASSERT_TRUE(zip.loadAllFileStatSlims());
  for (const auto& entry : entries) {
    size_t size = 0;
    if (entry.name.size() > 255) {
      EXPECT_FALSE(zip.getInflatedFileSize(entry.name.c_str(), &size));
      continue;
    }
    ASSERT_TRUE(zip.getInflatedFileSize(entry.name.c_str(), &size)) << entry.name;
    EXPECT_EQ(size, entry.data.size());
  }
}

TEST_F(ZipCentralDirectory, BatchSizeAndIdentityLookupsMatchEveryAddressableEntry) {
  std::vector<ZipFile::SizeTarget> sizeTargets;
  std::vector<ZipFile::EntryTarget> identityTargets;
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& name = entries[i].name;
    if (name.size() > 255) continue;
    const uint64_t hash = ZipFile::fnvHash64(name.c_str(), name.size());
    sizeTargets.push_back({hash, static_cast<uint16_t>(name.size()), static_cast<uint16_t>(i)});
    identityTargets.push_back({hash, static_cast<uint16_t>(name.size()), static_cast<uint16_t>(i), name.c_str()});
  }
  const auto byHash = [](const auto& a, const auto& b) {
    return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
  };
  std::sort(sizeTargets.begin(), sizeTargets.end(), byHash);
  std::sort(identityTargets.begin(), identityTargets.end(), byHash);

  std::vector<uint32_t> sizes(entries.size(), UINT32_MAX);
  EXPECT_EQ(ZipFile(path).fillUncompressedSizes(sizeTargets.data(), sizeTargets.size(), sizes.data(), sizes.size()),
            static_cast<int>(sizeTargets.size()));
  std::vector<ZipFile::EntryIdentity> identities(entries.size());
  EXPECT_EQ(ZipFile(path).fillEntryIdentities(identityTargets.data(), identityTargets.size(), identities.data(),
                                              identities.size()),
            static_cast<int>(identityTargets.size()));

  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].name.size() > 255) {
      EXPECT_EQ(sizes[i], UINT32_MAX);
      EXPECT_FALSE(identities[i].found);
      continue;
    }
    EXPECT_EQ(sizes[i], entries[i].data.size()) << entries[i].name;
    EXPECT_TRUE(identities[i].found) << entries[i].name;
    EXPECT_EQ(identities[i].crc32, entries[i].crc);
    EXPECT_EQ(identities[i].compressedSize, entries[i].data.size());
    EXPECT_EQ(identities[i].uncompressedSize, entries[i].data.size());
  }
}

TEST_F(ZipCentralDirectory, TruncatedCentralDirectoryStopsCleanly) {
  auto bytes = buildZip(entries);
  // Keep the EOCD but cut the directory short: lookups must fail, not overrun.
  const std::vector<uint8_t> eocd(bytes.end() - 22, bytes.end());
  uint32_t centralOffset = 0;
  memcpy(&centralOffset, eocd.data() + 16, 4);
  bytes.resize(centralOffset + 500);
  bytes.insert(bytes.end(), eocd.begin(), eocd.end());
  Storage.put(path, bytes);
  size_t size = 0;
  EXPECT_TRUE(ZipFile(path).getInflatedFileSize(entries[0].name.c_str(), &size));
  EXPECT_FALSE(ZipFile(path).getInflatedFileSize(entries[599].name.c_str(), &size));
}
