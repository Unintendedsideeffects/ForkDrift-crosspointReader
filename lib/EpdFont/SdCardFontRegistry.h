#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SdCardFontFileInfo {
  // Filename only (no directory), heap-allocated. nullptr means the canonical v4 name
  // "<Family>_<size>.cpfont", which is rebuilt on demand by SdCardFontFamilyInfo::filePath().
  std::unique_ptr<char[]> name;
  uint8_t pointSize;  // parsed from filename: 14
  uint8_t style;      // always 0 in v4 (all 4 styles bundled in one file);
                      // kept for potential future formats
};
static_assert(sizeof(SdCardFontFileInfo) == 2 * sizeof(void*), "SdCardFontFileInfo is resident per installed size");

struct SdCardFontFamilyInfo {
  static constexpr size_t PATH_BUFFER_SIZE = 160;

  std::string name;            // directory name, e.g. "NotoSansCJK"
  const char* root = nullptr;  // "/.fonts" (preferred, hidden) or "/fonts" (visible); points at a static string
  std::vector<SdCardFontFileInfo> files;

  // Rebuilds "<root>/<Family>/<file>" into buf. Returns false if it does not fit.
  bool filePath(const SdCardFontFileInfo& file, char* buf, size_t size) const;
  const SdCardFontFileInfo* findFile(uint8_t size, uint8_t style = 0) const;
  const SdCardFontFileInfo* findClosestReaderSize(uint8_t fontSizeEnum, uint8_t style = 0) const;
  bool hasSize(uint8_t size) const;
  std::vector<uint8_t> availableSizes() const;
};

class SdCardFontRegistry {
 public:
  static constexpr int MAX_SD_FAMILIES = 128;
  // Two top-level roots are scanned at discovery time. Hidden is preferred
  // when creating new installs; both are read from if present.
  static constexpr const char* FONTS_DIR_HIDDEN = "/.fonts";
  static constexpr const char* FONTS_DIR_VISIBLE = "/fonts";

  // Returns the existing root for `familyName` (the one that contains
  // /<root>/<familyName>/), or nullptr if the family is not installed in
  // either root. Used by writers to keep re-installs in their existing dir.
  static const char* findFamilyRoot(const char* familyName);

  // Returns the root path that should be used when creating a brand-new
  // family on disk (no prior install): the existing root if exactly one of
  // the two roots exists, otherwise the hidden root.
  static const char* defaultWriteRoot();

  // Scan SD card, populate families_. Returns true if any families found.
  // Invalidates pointers previously returned by getFamilies()/findFamily().
  bool discover();

  const std::vector<SdCardFontFamilyInfo>& getFamilies() const { return families_; }
  const SdCardFontFamilyInfo* findFamily(const std::string& name) const;
  int getFamilyIndex(const std::string& name) const;
  int getFamilyCount() const { return static_cast<int>(families_.size()); }

 private:
  std::vector<SdCardFontFamilyInfo> families_;  // sorted alphabetically

  static bool parseFilename(const char* filename, uint8_t& size, uint8_t& style);
  static void scanDirectory(const char* dirPath, SdCardFontFamilyInfo& family);
  // Scan one root (e.g. "/.fonts"), append families to `out`, dedup by name.
  static void scanRoot(const char* rootPath, std::vector<SdCardFontFamilyInfo>& out);
};
