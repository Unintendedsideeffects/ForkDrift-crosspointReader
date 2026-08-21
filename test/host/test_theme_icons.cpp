#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <functional>
#include <string>

#include "components/themes/ThemeIcons.h"
#include "doctest/doctest.h"

namespace {

// Distinct sentinels — the tests assert which entry was returned, not just
// that something non-null came back.
const uint8_t kIcon32[] = {0x32};
const uint8_t kIcon24[] = {0x24};

// Deliberately partial, mirroring a real theme: Settings exists only at 32px,
// Text only at 24px, and Bookmark/Hotspot/Wifi/Recent at neither.
const uint8_t* stubLookup(UIIcon icon, int size) {
  if (size == 32 && icon == UIIcon::Settings) return kIcon32;
  if (size == 24 && icon == UIIcon::Text) return kIcon24;
  return nullptr;
}

int gLookupCalls = 0;
const uint8_t* countingLookup(UIIcon icon, int size) {
  ++gLookupCalls;
  return stubLookup(icon, size);
}

std::string captureStdout(const std::function<void()>& fn) {
  fflush(stdout);
  int savedStdout = dup(fileno(stdout));
  FILE* tmp = tmpfile();
  if (savedStdout < 0 || !tmp) {
    if (savedStdout >= 0) close(savedStdout);
    if (tmp) fclose(tmp);
    fn();
    return "";
  }
  dup2(fileno(tmp), fileno(stdout));

  fn();

  fflush(stdout);
  dup2(savedStdout, fileno(stdout));
  close(savedStdout);

  rewind(tmp);
  std::string output;
  char buf[256];
  while (size_t n = fread(buf, 1, sizeof(buf), tmp)) {
    output.append(buf, n);
  }
  fclose(tmp);
  return output;
}

}  // namespace

TEST_CASE("testResolveReturnsPreferredSizeOnHit") {
  const auto res = theme_icons::resolve(stubLookup, UIIcon::Settings, 32, 24);
  CHECK(res.bmp == kIcon32);
  CHECK(res.size == 32);
}

TEST_CASE("testResolveFallsBackToOtherSize") {
  const auto res = theme_icons::resolve(stubLookup, UIIcon::Text, 32, 24);
  CHECK(res.bmp == kIcon24);
  CHECK(res.size == 24);
}

TEST_CASE("testResolveFallsBackDownwardToo") {
  const auto res = theme_icons::resolve(stubLookup, UIIcon::Settings, 24, 32);
  CHECK(res.bmp == kIcon32);
  CHECK(res.size == 32);
}

TEST_CASE("testResolveReturnsNullWhenBothSizesMiss") {
  const auto res = theme_icons::resolve(stubLookup, UIIcon::Bookmark, 32, 24);
  CHECK(res.bmp == nullptr);
  CHECK(res.size == 0);
}

TEST_CASE("testResolveSkipsSecondLookupWhenSizesMatch") {
  gLookupCalls = 0;
  const auto res = theme_icons::resolve(countingLookup, UIIcon::Hotspot, 32, 32);
  CHECK(gLookupCalls == 1);
  CHECK(res.bmp == nullptr);
  CHECK(res.size == 0);
}

TEST_CASE("testResolveSkipsSecondLookupWhenFallbackIsZero") {
  gLookupCalls = 0;
  const auto res = theme_icons::resolve(countingLookup, UIIcon::Wifi, 32, 0);
  CHECK(gLookupCalls == 1);
  CHECK(res.bmp == nullptr);
  CHECK(res.size == 0);
}

TEST_CASE("testWarnLogsOnceThenSuppresses") {
  const std::string out = captureStdout([]() {
    theme_icons::warnMissingIcon(UIIcon::Recent, 24);
    theme_icons::warnMissingIcon(UIIcon::Recent, 24);
  });
  const size_t first = out.find("ICON");
  CHECK(first != std::string::npos);
  const size_t second = (first != std::string::npos) ? out.find("ICON", first + 1) : std::string::npos;
  CHECK(second == std::string::npos);
}

TEST_CASE("testWarnMissingIconDoesNotCrashOnEveryEnumMember") {
  // Size 32 -> bucket 1. Deliberately NOT 24: warned[][] is a file-static
  // keyed by (icon, bucket), and testWarnLogsOnceThenSuppresses owns
  // (Recent, bucket 0). Looping at 24 here consumes that case's one-shot
  // warning and makes it fail whenever doctest runs this case first.
  for (uint8_t i = 0; i <= static_cast<uint8_t>(UIIcon::Bookmark); ++i) {
    theme_icons::warnMissingIcon(static_cast<UIIcon>(i), 32);
  }
  // Out-of-bounds must not crash (exercises the idx >= kUIIconCount guard).
  theme_icons::warnMissingIcon(static_cast<UIIcon>(99), 32);
  CHECK(true);
}
