#include "ReleaseJsonParser.h"

#include <cstdlib>
#include <cstring>

namespace {

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool isFirmwareAssetName(const char* name) {
  if (strcmp(name, "firmware.bin") == 0) {
    return true;
  }

  constexpr char prefix[] = "firmware-";
  constexpr char suffix[] = ".bin";
  constexpr size_t dateLength = 8;
  constexpr size_t minShaLength = 7;
  const size_t prefixLength = strlen(prefix);
  const size_t suffixLength = strlen(suffix);
  const size_t nameLength = name ? strlen(name) : 0;
  const size_t minLength = prefixLength + dateLength + 1 + minShaLength + suffixLength;

  if (nameLength < minLength || strncmp(name, prefix, prefixLength) != 0 ||
      strcmp(name + nameLength - suffixLength, suffix) != 0 || name[prefixLength + dateLength] != '-') {
    return false;
  }

  for (size_t i = prefixLength; i < prefixLength + dateLength; ++i) {
    if (name[i] < '0' || name[i] > '9') {
      return false;
    }
  }

  for (size_t i = prefixLength + dateLength + 1; i < nameLength - suffixLength; ++i) {
    const char ch = name[i];
    const bool isHex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    if (!isHex) {
      return false;
    }
  }

  return true;
}

}  // namespace

ReleaseJsonParser::ReleaseJsonParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}) {
  reset();
}

void ReleaseJsonParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  assetDepth = 0;
  tagName[0] = '\0';
  releaseName[0] = '\0';
  firmwareUrl[0] = '\0';
  firmwareSize = 0;
  tagFound = false;
  firmwareFound = false;
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSize = 0;
}

void ReleaseJsonParser::feed(const char* data, size_t len) { parser.feed(data, len); }

bool ReleaseJsonParser::foundTag() const { return tagFound; }
bool ReleaseJsonParser::foundFirmware() const { return firmwareFound; }
const char* ReleaseJsonParser::getTagName() const { return tagName; }
const char* ReleaseJsonParser::getReleaseName() const { return releaseName; }
const char* ReleaseJsonParser::getFirmwareUrl() const { return firmwareUrl; }
size_t ReleaseJsonParser::getFirmwareSize() const { return firmwareSize; }

void ReleaseJsonParser::commitAsset() {
  if (isFirmwareAssetName(currentAssetName)) {
    memcpy(firmwareUrl, currentAssetUrl, sizeof(firmwareUrl));
    firmwareSize = currentAssetSize;
    firmwareFound = true;
  }
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSize = 0;
}

// -- SAX callbacks (static trampolines) -------------------------------------

void ReleaseJsonParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth == 1) {
        if (len == 8 && memcmp(key, "tag_name", 8) == 0)
          self->lastKey = LastKey::TAG_NAME;
        else if (len == 4 && memcmp(key, "name", 4) == 0)
          self->lastKey = LastKey::RELEASE_NAME;
        else if (len == 6 && memcmp(key, "assets", 6) == 0)
          self->lastKey = LastKey::ASSETS;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    case Position::IN_ASSET_OBJECT:
      if (self->assetDepth == 1) {
        if (len == 4 && memcmp(key, "name", 4) == 0)
          self->lastKey = LastKey::ASSET_NAME;
        else if (len == 20 && memcmp(key, "browser_download_url", 20) == 0)
          self->lastKey = LastKey::ASSET_URL;
        else if (len == 4 && memcmp(key, "size", 4) == 0)
          self->lastKey = LastKey::ASSET_SIZE;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->lastKey) {
    case LastKey::TAG_NAME:
      if (self->position == Position::TOP_LEVEL && self->depth == 1) {
        safeCopy(self->tagName, sizeof(self->tagName), value, len);
        self->tagFound = true;
      }
      break;
    case LastKey::RELEASE_NAME:
      if (self->position == Position::TOP_LEVEL && self->depth == 1) {
        safeCopy(self->releaseName, sizeof(self->releaseName), value, len);
      }
      break;
    case LastKey::ASSET_NAME:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)
        safeCopy(self->currentAssetName, sizeof(self->currentAssetName), value, len);
      break;
    case LastKey::ASSET_URL:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)
        safeCopy(self->currentAssetUrl, sizeof(self->currentAssetUrl), value, len);
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnNumber(void* ctx, const char* value, size_t /*len*/) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  if (self->lastKey == LastKey::ASSET_SIZE && self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
    self->currentAssetSize = static_cast<size_t>(strtoul(value, nullptr, 10));
  }
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<ReleaseJsonParser*>(ctx)->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnNull(void* ctx) { static_cast<ReleaseJsonParser*>(ctx)->lastKey = LastKey::NONE; }

void ReleaseJsonParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::IN_ASSET_OBJECT;
      self->assetDepth = 1;
      self->currentAssetName[0] = '\0';
      self->currentAssetUrl[0] = '\0';
      self->currentAssetSize = 0;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
  }
}

void ReleaseJsonParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      if (self->assetDepth == 0) {
        self->commitAsset();
        self->position = Position::IN_ASSETS_ARRAY;
      }
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::ASSETS && self->depth == 1) {
        self->position = Position::IN_ASSETS_ARRAY;
      } else {
        self->depth++;
      }
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      self->lastKey = LastKey::NONE;
      break;
  }
}
