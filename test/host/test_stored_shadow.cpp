#include <cstdlib>
#include <cstring>
#include <string>

#include "StoredShadow.h"
#include "ZipFile.h"
#include "doctest/doctest.h"

TEST_CASE("STORED shadow archive round-trips without a deflate window") {
  Storage.reset();
  Storage.mkdir("/shadow");

  const char mimetype[] = "application/epub+zip";
  const char payload[] = "<html><body>ok</body></html>";
  const StoredShadowEntry entries[] = {
      {"mimetype", reinterpret_cast<const uint8_t*>(mimetype), static_cast<uint32_t>(std::strlen(mimetype))},
      {"OPS/chapter.xhtml", reinterpret_cast<const uint8_t*>(payload), static_cast<uint32_t>(std::strlen(payload))},
  };

  const std::string path = stored_shadow::pathBesideCache("/shadow");
  CHECK(path == "/shadow/stored.epub");
  REQUIRE(stored_shadow::writeArchive(path.c_str(), entries, 2));

  ZipFile zip(path);
  CHECK_FALSE(zip.hasAnyDeflated());

  size_t mimeSize = 0;
  uint8_t* mime = zip.readFileToMemory("mimetype", &mimeSize, false);
  REQUIRE(mime != nullptr);
  REQUIRE(mimeSize == std::strlen(mimetype));
  CHECK(std::memcmp(mime, mimetype, mimeSize) == 0);
  free(mime);

  size_t htmlSize = 0;
  uint8_t* html = zip.readFileToMemory("OPS/chapter.xhtml", &htmlSize, false);
  REQUIRE(html != nullptr);
  REQUIRE(htmlSize == std::strlen(payload));
  CHECK(std::memcmp(html, payload, htmlSize) == 0);
  free(html);
}

TEST_CASE("STORED shadow writer rejects empty input") {
  Storage.reset();
  CHECK_FALSE(stored_shadow::writeArchive("/empty.epub", nullptr, 0));
  const StoredShadowEntry bad{nullptr, nullptr, 0};
  CHECK_FALSE(stored_shadow::writeArchive("/empty.epub", &bad, 1));
}
