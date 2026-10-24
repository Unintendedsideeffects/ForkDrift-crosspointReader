#include "Epub/parsers/InlineStyleEntry.h"
#include "doctest/doctest.h"

TEST_CASE("paragraph dir: p dir=rtl plus span dir=ltr stays RTL") {
  const ParagraphDirection block{true, true};
  StyleStackEntry span;
  span.hasDirection = true;
  span.direction = CssTextDirection::Ltr;
  span.setsParagraphDirection = false;
  const auto resolved = resolveParagraphDirection(block, &span, 1);
  CHECK(resolved.defined);
  CHECK(resolved.isRtl);
}

TEST_CASE("paragraph dir: html dir=rtl with no p dir stays RTL") {
  const ParagraphDirection block{false, false};
  StyleStackEntry html;
  html.hasDirection = true;
  html.direction = CssTextDirection::Rtl;
  html.setsParagraphDirection = true;
  const auto resolved = resolveParagraphDirection(block, &html, 1);
  CHECK(resolved.defined);
  CHECK(resolved.isRtl);
}

TEST_CASE("paragraph dir: no dir anywhere stays undefined for auto-detect") {
  const ParagraphDirection block{false, false};
  const auto resolved = resolveParagraphDirection(block, nullptr, 0);
  CHECK_FALSE(resolved.defined);
  CHECK_FALSE(resolved.isRtl);
}
