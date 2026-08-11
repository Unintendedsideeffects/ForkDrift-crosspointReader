#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"

class ReleaseJsonParser {
 public:
  ReleaseJsonParser();

  ReleaseJsonParser(const ReleaseJsonParser&) = delete;
  ReleaseJsonParser& operator=(const ReleaseJsonParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  bool foundTag() const;
  bool foundFirmware() const;
  const char* getTagName() const;
  const char* getReleaseName() const;
  const char* getFirmwareUrl() const;
  size_t getFirmwareSize() const;
  // GitHub's asset digest, in its native "sha256:<hex>" form. Empty if the
  // release predates the field or the asset has none. parseSha256Hex() in
  // OtaUpdater already strips the prefix, so this is passed through verbatim.
  const char* getFirmwareDigest() const;

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_ASSETS_ARRAY,
    IN_ASSET_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    TAG_NAME,
    RELEASE_NAME,
    ASSETS,
    ASSET_NAME,
    ASSET_URL,
    ASSET_SIZE,
    ASSET_DIGEST,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitAsset();

  StreamingJsonParser parser;

  Position position;
  LastKey lastKey;
  uint8_t depth;
  uint8_t assetDepth;

  char tagName[32];
  char releaseName[64];
  char firmwareUrl[512];
  // "sha256:" + 64 hex + NUL = 72; rounded up. GitHub computes this server-side
  // for every release asset, so it is present without us publishing anything.
  char firmwareDigest[80];
  size_t firmwareSize;
  bool tagFound;
  bool firmwareFound;

  char currentAssetName[32];
  char currentAssetUrl[512];
  char currentAssetDigest[80];
  size_t currentAssetSize;
};
