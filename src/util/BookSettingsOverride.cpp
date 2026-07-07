#include "BookSettingsOverride.h"

#if ENABLE_PER_BOOK_SETTINGS

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

#include "CrossPointSettings.h"

namespace {
constexpr char kFileName[] = "/book_settings.json";

void setStr(char* dst, const size_t dstSize, const char* src) {
  snprintf(dst, dstSize, "%s", src != nullptr ? src : "");
}
}  // namespace

bool BookSettingsOverride::load(const std::string& cachePath, BookSettingsOverride& out) {
  out = BookSettingsOverride{};
  const std::string path = cachePath + kFileName;
  HalFile file;
  if (!Storage.openFileForRead("BKS", path, file)) {
    return false;  // no overrides for this book
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  if (err) {
    LOG_ERR("BKS", "Corrupt %s: %s", path.c_str(), err.c_str());
    return false;
  }

  out.enabled = doc["enabled"] | false;
  auto loadField = [&doc](const char* key, bool& hasFlag, uint8_t& value) {
    if (!doc[key].isNull()) {
      hasFlag = true;
      value = doc[key] | 0;
    }
  };
  loadField("fontFamily", out.hasFontFamily, out.fontFamily);
  if (out.hasFontFamily) {
    setStr(out.sdFontFamilyName, sizeof(out.sdFontFamilyName), doc["sdFontFamilyName"] | "");
  }
  loadField("fontSize", out.hasFontSize, out.fontSize);
  loadField("lineSpacing", out.hasLineSpacing, out.lineSpacing);
  loadField("screenMargin", out.hasScreenMargin, out.screenMargin);
  loadField("paragraphAlignment", out.hasParagraphAlignment, out.paragraphAlignment);
  loadField("extraParagraphSpacing", out.hasExtraParagraphSpacing, out.extraParagraphSpacing);
  loadField("forceParagraphIndents", out.hasForceParagraphIndents, out.forceParagraphIndents);
  loadField("hyphenationEnabled", out.hasHyphenation, out.hyphenationEnabled);
  loadField("textAntiAliasing", out.hasTextAntiAliasing, out.textAntiAliasing);
  loadField("imageRendering", out.hasImageRendering, out.imageRendering);
  return true;
}

bool BookSettingsOverride::save(const std::string& cachePath) const {
  const std::string path = cachePath + kFileName;
  JsonDocument doc;
  doc["enabled"] = enabled;
  if (hasFontFamily) {
    doc["fontFamily"] = fontFamily;
    doc["sdFontFamilyName"] = sdFontFamilyName;
  }
  if (hasFontSize) doc["fontSize"] = fontSize;
  if (hasLineSpacing) doc["lineSpacing"] = lineSpacing;
  if (hasScreenMargin) doc["screenMargin"] = screenMargin;
  if (hasParagraphAlignment) doc["paragraphAlignment"] = paragraphAlignment;
  if (hasExtraParagraphSpacing) doc["extraParagraphSpacing"] = extraParagraphSpacing;
  if (hasForceParagraphIndents) doc["forceParagraphIndents"] = forceParagraphIndents;
  if (hasHyphenation) doc["hyphenationEnabled"] = hyphenationEnabled;
  if (hasTextAntiAliasing) doc["textAntiAliasing"] = textAntiAliasing;
  if (hasImageRendering) doc["imageRendering"] = imageRendering;

  HalFile file;
  if (!Storage.openFileForWrite("BKS", path, file)) {
    LOG_ERR("BKS", "Failed to open %s for write", path.c_str());
    return false;
  }
  serializeJson(doc, file);
  return true;
}

void BookSettingsOverride::remove(const std::string& cachePath) {
  const std::string path = cachePath + kFileName;
  if (Storage.exists(path.c_str())) {
    Storage.remove(path.c_str());
  }
}

void BookSettingsOverride::captureFromGlobals() {
  fontFamily = SETTINGS.fontFamily;
  setStr(sdFontFamilyName, sizeof(sdFontFamilyName), SETTINGS.sdFontFamilyName);
  fontSize = SETTINGS.fontSize;
  lineSpacing = SETTINGS.lineSpacing;
  screenMargin = SETTINGS.screenMargin;
  paragraphAlignment = SETTINGS.paragraphAlignment;
  extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  forceParagraphIndents = SETTINGS.forceParagraphIndents;
  hyphenationEnabled = SETTINGS.hyphenationEnabled;
  textAntiAliasing = SETTINGS.textAntiAliasing;
  imageRendering = SETTINGS.imageRendering;
}

void BookSettingsOverride::applyToGlobals() const {
  if (hasFontFamily) {
    SETTINGS.fontFamily = fontFamily;
    setStr(SETTINGS.sdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName), sdFontFamilyName);
  }
  if (hasFontSize) SETTINGS.fontSize = fontSize;
  if (hasLineSpacing) SETTINGS.lineSpacing = lineSpacing;
  if (hasScreenMargin) SETTINGS.screenMargin = screenMargin;
  if (hasParagraphAlignment) SETTINGS.paragraphAlignment = paragraphAlignment;
  if (hasExtraParagraphSpacing) SETTINGS.extraParagraphSpacing = extraParagraphSpacing;
  if (hasForceParagraphIndents) SETTINGS.forceParagraphIndents = forceParagraphIndents;
  if (hasHyphenation) SETTINGS.hyphenationEnabled = hyphenationEnabled;
  if (hasTextAntiAliasing) SETTINGS.textAntiAliasing = textAntiAliasing;
  if (hasImageRendering) SETTINGS.imageRendering = imageRendering;
}

namespace BookSettingsScope {
namespace {
BookSettingsOverride* g_active = nullptr;
std::string g_cachePath;
BookSettingsOverride g_snapshot;
}  // namespace

void setActive(BookSettingsOverride* override, const std::string& cachePath, const BookSettingsOverride& snapshot) {
  g_active = override;
  g_cachePath = cachePath;
  g_snapshot = snapshot;
}

bool saveGlobalsPreservingOverrides() {
  if (g_active == nullptr || !g_active->enabled || !g_active->anySet()) {
    return SETTINGS.saveToFileRaw();
  }
  // Current per-book values in RAM; restore true globals, save, re-apply.
  BookSettingsOverride current = *g_active;
  current.captureFromGlobals();  // per-book RAM values for the set fields
  BookSettingsOverride restore = g_snapshot;
  // Restore snapshot values only for fields the override sets.
  restore.hasFontFamily = g_active->hasFontFamily;
  restore.hasFontSize = g_active->hasFontSize;
  restore.hasLineSpacing = g_active->hasLineSpacing;
  restore.hasScreenMargin = g_active->hasScreenMargin;
  restore.hasParagraphAlignment = g_active->hasParagraphAlignment;
  restore.hasExtraParagraphSpacing = g_active->hasExtraParagraphSpacing;
  restore.hasForceParagraphIndents = g_active->hasForceParagraphIndents;
  restore.hasHyphenation = g_active->hasHyphenation;
  restore.hasTextAntiAliasing = g_active->hasTextAntiAliasing;
  restore.hasImageRendering = g_active->hasImageRendering;
  restore.applyToGlobals();
  const bool ok = SETTINGS.saveToFileRaw();
  if (!ok) {
    LOG_ERR("BKS", "Failed to save settings");
  }
  current.hasFontFamily = restore.hasFontFamily;
  current.hasFontSize = restore.hasFontSize;
  current.hasLineSpacing = restore.hasLineSpacing;
  current.hasScreenMargin = restore.hasScreenMargin;
  current.hasParagraphAlignment = restore.hasParagraphAlignment;
  current.hasExtraParagraphSpacing = restore.hasExtraParagraphSpacing;
  current.hasForceParagraphIndents = restore.hasForceParagraphIndents;
  current.hasHyphenation = restore.hasHyphenation;
  current.hasTextAntiAliasing = restore.hasTextAntiAliasing;
  current.hasImageRendering = restore.hasImageRendering;
  current.applyToGlobals();
  return ok;
}

void clearActive() {
  g_active = nullptr;
  g_cachePath.clear();
}

bool isActive() { return g_active != nullptr; }

BookSettingsOverride* active() { return g_active; }

bool recordChange(const char* key) {
  if (g_active == nullptr || key == nullptr || !g_active->enabled) {
    return false;
  }
  BookSettingsOverride& o = *g_active;
  if (strcmp(key, "fontFamily") == 0 || strcmp(key, "userFontPath") == 0) {
    o.hasFontFamily = true;
    o.fontFamily = SETTINGS.fontFamily;
    setStr(o.sdFontFamilyName, sizeof(o.sdFontFamilyName), SETTINGS.sdFontFamilyName);
  } else if (strcmp(key, "fontSize") == 0) {
    o.hasFontSize = true;
    o.fontSize = SETTINGS.fontSize;
  } else if (strcmp(key, "lineSpacing") == 0) {
    o.hasLineSpacing = true;
    o.lineSpacing = SETTINGS.lineSpacing;
  } else if (strcmp(key, "screenMargin") == 0) {
    o.hasScreenMargin = true;
    o.screenMargin = SETTINGS.screenMargin;
  } else if (strcmp(key, "paragraphAlignment") == 0) {
    o.hasParagraphAlignment = true;
    o.paragraphAlignment = SETTINGS.paragraphAlignment;
  } else if (strcmp(key, "extraParagraphSpacing") == 0) {
    o.hasExtraParagraphSpacing = true;
    o.extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  } else if (strcmp(key, "forceParagraphIndents") == 0) {
    o.hasForceParagraphIndents = true;
    o.forceParagraphIndents = SETTINGS.forceParagraphIndents;
  } else if (strcmp(key, "hyphenationEnabled") == 0) {
    o.hasHyphenation = true;
    o.hyphenationEnabled = SETTINGS.hyphenationEnabled;
  } else if (strcmp(key, "textAntiAliasing") == 0) {
    o.hasTextAntiAliasing = true;
    o.textAntiAliasing = SETTINGS.textAntiAliasing;
  } else if (strcmp(key, "imageRendering") == 0) {
    o.hasImageRendering = true;
    o.imageRendering = SETTINGS.imageRendering;
  } else {
    return false;  // not a per-book-capable setting; persist globally
  }
  o.save(g_cachePath);
  return true;
}

void setEnabled(const bool enabled) {
  if (g_active == nullptr) {
    return;
  }
  if (enabled) {
    g_active->enabled = true;
    g_active->save(g_cachePath);
  } else {
    *g_active = BookSettingsOverride{};
    BookSettingsOverride::remove(g_cachePath);
    // Reload true globals over the per-book values currently in RAM.
    SETTINGS.loadFromFile();
  }
}

bool isEnabled() { return g_active != nullptr && g_active->enabled; }

}  // namespace BookSettingsScope

#endif  // ENABLE_PER_BOOK_SETTINGS
