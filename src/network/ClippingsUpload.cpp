#include "ClippingsUpload.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <Memory.h>
#if defined(SIMULATOR) && defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#elif defined(SIMULATOR)
#include <openssl/sha.h>
#else
#include <mbedtls/sha256.h>
#endif

#include "ClippingStore.h"

bool ClippingsUpload::identity(const uint32_t timestamp, const std::string& text, char (&id)[17]) {
  char decimal[11];
  const int length = snprintf(decimal, sizeof(decimal), "%lu", static_cast<unsigned long>(timestamp));
  uint8_t digest[32];
  // The simulator's mbedtls header is a non-cryptographic stub. Use its host's
  // real SHA256, as its MD5Builder already does for progress authentication.
#if defined(SIMULATOR) && defined(__APPLE__)
  CC_SHA256_CTX context;
  CC_SHA256_Init(&context);
  CC_SHA256_Update(&context, decimal, length);
  CC_SHA256_Update(&context, text.data(), text.size());
  CC_SHA256_Final(digest, &context);
#elif defined(SIMULATOR)
  SHA256_CTX context;
  SHA256_Init(&context);
  SHA256_Update(&context, decimal, length);
  SHA256_Update(&context, text.data(), text.size());
  SHA256_Final(digest, &context);
#else
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  const bool ok = mbedtls_sha256_starts(&context, 0) == 0 &&
                  mbedtls_sha256_update(&context, reinterpret_cast<const uint8_t*>(decimal), length) == 0 &&
                  mbedtls_sha256_update(&context, reinterpret_cast<const uint8_t*>(text.data()), text.size()) == 0 &&
                  mbedtls_sha256_finish(&context, digest) == 0;
  mbedtls_sha256_free(&context);
  if (!ok) {
    LOG_ERR("ClippingsSync", "Cannot hash clipping");
    return false;
  }
#endif
  for (size_t i = 0; i < 8; ++i) snprintf(id + i * 2, 3, "%02x", digest[i]);
  return true;
}

StatsUploadClient::Result ClippingsUpload::upload(const std::string& path, const std::string& document) {
  using Result = StatsUploadClient::Result;
  if (document.size() != 32 || document.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
    return Result::InvalidResponse;
  const auto endpoint = "/api/v1/clippings/" + document;
  StatsUploadClient client;
  std::string text;
  text.reserve(CLIPPING_TEXT_MAX);
  // One clipping at a time; never hold the up-to-256-record index across TLS.
  // Reuse the request buffer across records, growing fallibly only when needed.
  std::unique_ptr<char[]> payload;
  size_t capacity = 0;
  for (size_t i = 0; i <= CLIPPING_MAX_PER_BOOK; ++i) {
    Clipping clipping;
    bool done = false;
    if (!ClippingStore::readForUpload(path, i, clipping, text, done)) return Result::InvalidResponse;
    if (done) return i == 0 ? Result::Skipped : Result::Ok;
    const uint32_t createdAt = clippingUnixTimestamp(clipping.timestamp);
    char id[17];
    if (text.empty() || text.size() > CLIPPING_TEXT_MAX || !identity(createdAt, text, id))
      return Result::InvalidResponse;
    JsonDocument json;
    auto item = json["items"].to<JsonArray>().add<JsonObject>();
    item["id"] = static_cast<const char*>(id);
    item["spine"] = clipping.spineIndex;
    item["start_page"] = clipping.startPage;
    item["end_page"] = clipping.endPage;
    item["pages"] = clipping.pageCount;
    item["start_word"] = clipping.startWordIndex;
    item["end_word"] = clipping.endWordIndex;
    item["words"] = clipping.wordCount;
    if (clipping.paragraphIndex != UINT16_MAX) item["para"] = clipping.paragraphIndex;
    item["chapter"] = static_cast<const char*>(clipping.chapterTitle);
    item["text"] = text.c_str();
    item["created_at"] = createdAt;
    item["layout_signature"] = clipping.layoutSignature;
    // Omit note/color: preserve server annotations. Upload-only never emits tombstones.
    if (json.overflowed()) return Result::LowMemory;
    const size_t needed = measureJson(json) + 1;
    if (needed > CLIPPING_TEXT_MAX * 6 + 1024) return Result::InvalidResponse;
    if (needed > capacity) {
      payload.reset();
      payload = makeUniqueNoThrow<char[]>(needed);
      if (!payload) {
        LOG_ERR("ClippingsSync", "Cannot allocate %u-byte request", unsigned(needed));
        return Result::LowMemory;
      }
      capacity = needed;
    }
    serializeJson(json, payload.get(), capacity);
    json.clear();
    const auto result = client.put(endpoint.c_str(), payload.get(), true);
    if (result != Result::Ok) return result;
  }
  return Result::InvalidResponse;
}
