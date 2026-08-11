#include <cstring>
#include <string>

#include "ReleaseJsonParser.h"
#include "doctest/doctest.h"

// The digest drives whether an OTA install is allowed at all: OtaUpdater fails
// closed on X4 when the checksum is empty. So "we parsed the digest" and "we
// parsed it off the *right* asset" are both load-bearing, not cosmetic.

namespace {

// Shaped like the real GitHub releases API response, trimmed to the fields the
// parser looks at plus enough noise to exercise the state machine: a nested
// object (uploader) and a non-firmware asset ahead of the firmware one.
std::string releaseJson(const char* firmwareDigestField) {
  std::string json = R"({"tag_name":"latest","name":"20260726",)"
                     R"("assets":[)"
                     R"({"name":"crosspoint-partitions.bin","size":3072,)"
                     R"("uploader":{"login":"someone","id":42},)"
                     R"("digest":"sha256:bbbb000000000000000000000000000000000000000000000000000000000000",)"
                     R"("browser_download_url":"https://example.invalid/partitions.bin"},)"
                     R"({"name":"firmware-20260726-39072d6.bin","size":1234567,)"
                     R"("uploader":{"login":"someone","id":42},)";
  json += firmwareDigestField;
  json += R"("browser_download_url":"https://example.invalid/firmware.bin"})";
  json += R"(]})";
  return json;
}

void feedAll(ReleaseJsonParser& parser, const std::string& json) { parser.feed(json.c_str(), json.size()); }

}  // namespace

TEST_CASE("ReleaseJsonParser captures the firmware asset digest") {
  ReleaseJsonParser parser;
  const auto json =
      releaseJson(R"("digest":"sha256:759886fddfb4e65a38fd56020e91bd7c9b30d893152806db5471697a9125e7b5",)");
  feedAll(parser, json);

  CHECK(parser.foundTag());
  CHECK(parser.foundFirmware());
  CHECK(std::string(parser.getFirmwareUrl()) == "https://example.invalid/firmware.bin");
  CHECK(parser.getFirmwareSize() == 1234567u);
  // Kept in GitHub's native form — parseSha256Hex() strips the prefix itself.
  CHECK(std::string(parser.getFirmwareDigest()) ==
        "sha256:759886fddfb4e65a38fd56020e91bd7c9b30d893152806db5471697a9125e7b5");
}

TEST_CASE("ReleaseJsonParser does not leak a sibling asset's digest") {
  // The partitions asset carries a digest; the firmware asset does not. If the
  // parser let per-asset state bleed across objects, the firmware would appear
  // verified against the wrong file's hash — worse than no digest at all.
  ReleaseJsonParser parser;
  feedAll(parser, releaseJson(""));

  CHECK(parser.foundFirmware());
  CHECK(std::string(parser.getFirmwareDigest()).empty());
}

TEST_CASE("ReleaseJsonParser treats a null digest as absent") {
  ReleaseJsonParser parser;
  feedAll(parser, releaseJson(R"("digest":null,)"));

  CHECK(parser.foundFirmware());
  CHECK(std::string(parser.getFirmwareDigest()).empty());
}

TEST_CASE("ReleaseJsonParser digest survives a chunk split mid-value") {
  // The real feed arrives in HTTP chunks, so the value can be split anywhere.
  const auto json =
      releaseJson(R"("digest":"sha256:759886fddfb4e65a38fd56020e91bd7c9b30d893152806db5471697a9125e7b5",)");
  for (const size_t splitAt : {size_t{1}, json.size() / 3, json.size() / 2, json.size() - 1}) {
    ReleaseJsonParser parser;
    parser.feed(json.c_str(), splitAt);
    parser.feed(json.c_str() + splitAt, json.size() - splitAt);

    CHECK(parser.foundFirmware());
    CHECK(std::string(parser.getFirmwareDigest()) ==
          "sha256:759886fddfb4e65a38fd56020e91bd7c9b30d893152806db5471697a9125e7b5");
  }
}

TEST_CASE("ReleaseJsonParser reset clears a previously parsed digest") {
  // The updater reuses a parser across channel candidates; a stale digest from
  // a prior release would be applied to the next one's image.
  ReleaseJsonParser parser;
  feedAll(parser,
          releaseJson(R"("digest":"sha256:759886fddfb4e65a38fd56020e91bd7c9b30d893152806db5471697a9125e7b5",)"));
  REQUIRE(std::string(parser.getFirmwareDigest()).length() > 0);

  parser.reset();
  CHECK_FALSE(parser.foundFirmware());
  CHECK(std::string(parser.getFirmwareDigest()).empty());
}

TEST_CASE("ReleaseJsonParser ignores an over-long digest without overflowing") {
  // Malformed metadata must not corrupt the fixed buffer; a truncated value is
  // fine because parseSha256Hex rejects anything that is not 64 hex chars.
  std::string huge = R"("digest":"sha256:)";
  huge.append(400, 'a');
  huge += R"(",)";

  ReleaseJsonParser parser;
  feedAll(parser, releaseJson(huge.c_str()));

  CHECK(parser.foundFirmware());
  CHECK(std::strlen(parser.getFirmwareDigest()) < 80);
}
