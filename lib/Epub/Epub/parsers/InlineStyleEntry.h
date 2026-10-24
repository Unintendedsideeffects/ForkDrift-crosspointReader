#pragma once

#include <cstddef>

#include "Epub/css/CssStyle.h"

/*
 * Host-testable inline style helpers. ChapterHtmlSlimParser.cpp cannot join the
 * host suite (expat/GfxRenderer), so paragraph-dir vs embedding and the
 * internal-link Super/Sub fill live here as the FootnoteGuard.h pattern.
 */

struct StyleStackEntry {
  int depth = 0;
  bool hasBold = false, bold = false;
  bool hasItalic = false, italic = false;
  bool hasUnderline = false, underline = false;
  bool hasStrikethrough = false, strikethrough = false;
  bool hasDirection = false;
  CssTextDirection direction = CssTextDirection::Ltr;
  bool setsParagraphDirection = false;
  bool hasSup = false, sup = false;
  bool hasSub = false, sub = false;
};

struct ParagraphDirection {
  bool defined = false;
  bool isRtl = false;
};

inline void applyDirectionToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (css.hasDirection()) {
    entry.hasDirection = true;
    entry.direction = css.direction;
  }
}

inline void applyVerticalAlignToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (!css.hasVerticalAlign()) {
    return;
  }
  if (css.verticalAlign == CssVerticalAlign::Super) {
    entry.hasSup = true;
    entry.sup = true;
  } else if (css.verticalAlign == CssVerticalAlign::Sub) {
    entry.hasSub = true;
    entry.sub = true;
  }
}

inline void fillInternalLinkStyleEntry(StyleStackEntry& entry, const CssStyle& css) {
  entry.hasUnderline = true;
  entry.underline = true;
  applyDirectionToEntry(entry, css);
  applyVerticalAlignToEntry(entry, css);
}

inline ParagraphDirection resolveParagraphDirection(ParagraphDirection blockDir, const StyleStackEntry* entries,
                                                    size_t count) {
  ParagraphDirection paragraph = blockDir;
  if (entries == nullptr) {
    return paragraph;
  }
  for (size_t i = 0; i < count; ++i) {
    const StyleStackEntry& entry = entries[i];
    if (entry.hasDirection && entry.setsParagraphDirection) {
      paragraph.defined = true;
      paragraph.isRtl = (entry.direction == CssTextDirection::Rtl);
    }
  }
  return paragraph;
}
