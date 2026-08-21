#include "TextBlock.h"

#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <HeapGuard.h>
#include <Logging.h>
#include <Serialization.h>

#include <cstring>

namespace {
constexpr uint16_t MAX_WORDS_PER_TEXT_BLOCK = 512;
// 1024, not CrossInk's 200. The bound exists to stop corrupt data driving a
// large allocation, and 1024 B is still trivial against the C3's ~380 KB heap
// (heapguard::canAllocate preflights it anyway). CrossInk can afford 200
// because it splits long words at layout time; ForkDrift has not ported
// long-word continuation (3319aa172), so a long URL in body text stays a single
// word. At 200 the writer would emit what the reader rejects, and the section
// cache would rebuild forever. See plans/111d.
constexpr uint32_t MAX_SERIALIZED_WORD_BYTES = 1024;

bool readBoundedWord(serialization::BufferedReader& reader, std::string& word) {
  uint32_t len = 0;
  if (!serialization::readPod(reader, len) || len > MAX_SERIALIZED_WORD_BYTES) {
    return false;
  }
  if (len == 0) {
    word.clear();
    return true;
  }
  if (!heapguard::canAllocate(static_cast<size_t>(len) + 1, 0)) {
    return false;
  }
  word.resize(len);
  return reader.read(word.data(), len) == static_cast<int>(len);
}
}  // namespace

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  // Focus annotations are optional: empty vectors mean no word in this block has a split.
  // When present, they must be sized in lockstep with words[].
  const bool hasFocus = !wordFocusBoundary.empty();
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      (hasFocus && (words.size() != wordFocusBoundary.size() || words.size() != wordFocusSuffixX.size()))) {
    LOG_ERR("TXB", "Render skipped: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u)\n",
            (uint32_t)words.size(), (uint32_t)wordXpos.size(), (uint32_t)wordStyles.size(),
            (uint32_t)wordFocusBoundary.size(), (uint32_t)wordFocusSuffixX.size());
    return;
  }

  const bool scanning = renderer.isFontCacheScanning();
  const int ascender = renderer.getFontAscenderSize(fontId);
  for (size_t i = 0; i < words.size(); i++) {
    const int wordX = wordXpos[i] + x;
    const EpdFontFamily::Style currentStyle = wordStyles[i];
    const auto baseDir = static_cast<BidiUtils::BidiBaseDir>(
        BidiUtils::detectParagraphLevel(words[i].c_str(), blockStyle.isRtl ? 1 : 0));
    const uint8_t boundary = hasFocus ? wordFocusBoundary[i] : 0;

    // SUP/SUB shift the baseline passed to drawText (glyphs render full-size;
    // no 50% scaling is applied). Offsets are relative to the full-size ascender:
    //   SUP: raise by 40% of ascender — sits clearly above the cap-height
    //   SUB: lower by 25% of ascender — descends below baseline without clashing with ascenders below
    int wordY = y;
    if ((currentStyle & EpdFontFamily::SUP) != 0) {
      wordY -= ascender * 2 / 5;
    } else if ((currentStyle & EpdFontFamily::SUB) != 0) {
      wordY += ascender / 4;
    }

    if (boundary > 0) {
      // Focus split: draw bold prefix, then the regular suffix at a pre-computed x offset.
      // The bold prefix is bounded to 9 codepoints by the clamp on targetBoldChars in
      // ParsedText::addWord; 9 UTF-8 codepoints occupy at most 9 * 4 = 36 bytes, +1 for null = 37.
      // suffixX is computed at cache-creation time to avoid font metric lookups at render time.
      static constexpr size_t MAX_FOCUS_PREFIX_BYTES = 9 * 4 + 1;
      char boldBuf[40];
      static_assert(sizeof(boldBuf) >= MAX_FOCUS_PREFIX_BYTES,
                    "boldBuf too small for max focus prefix (9 codepoints * 4 UTF-8 bytes + null)");
      const auto boldStyle = static_cast<EpdFontFamily::Style>(currentStyle | EpdFontFamily::BOLD);
      const size_t boldLen = std::min<size_t>({static_cast<size_t>(boundary), words[i].size(), sizeof(boldBuf) - 1});
      memcpy(boldBuf, words[i].c_str(), boldLen);
      boldBuf[boldLen] = '\0';
      renderer.drawText(fontId, wordX, wordY, boldBuf, true, boldStyle, baseDir);
      const int suffixX = wordX + wordFocusSuffixX[i];
      renderer.drawText(fontId, suffixX, wordY, words[i].c_str() + boldLen, true, currentStyle, baseDir);
    } else {
      renderer.drawText(fontId, wordX, wordY, words[i].c_str(), true, currentStyle, baseDir);
    }

    if (!scanning && (currentStyle & EpdFontFamily::UNDERLINE) != 0) {
      const std::string& w = words[i];
      const int fullWordWidth = renderer.getTextWidth(fontId, w.c_str(), currentStyle, baseDir);
      // y is the top of the text line; add ascender to reach baseline, then offset 2px below
      const int underlineY = wordY + ascender + 2;

      int startX = wordX;
      int underlineWidth = fullWordWidth;

      // if word starts with em-space ("\xe2\x80\x83"), account for the additional indent before drawing the line
      if (w.size() >= 3 && static_cast<uint8_t>(w[0]) == 0xE2 && static_cast<uint8_t>(w[1]) == 0x80 &&
          static_cast<uint8_t>(w[2]) == 0x83) {
        const char* visiblePtr = w.c_str() + 3;
        const int prefixWidth = renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", currentStyle);
        const int visibleWidth = renderer.getTextWidth(fontId, visiblePtr, currentStyle, baseDir);
        startX = wordX + prefixWidth;
        underlineWidth = visibleWidth;
      }

      renderer.drawLine(startX, underlineY, startX + underlineWidth, underlineY, 3, true);
    }

    if ((currentStyle & EpdFontFamily::STRIKETHROUGH) != 0) {
      const std::string& w = words[i];
      const int fullWordWidth = renderer.getTextWidth(fontId, w.c_str(), currentStyle);
      const int strikeY = y + renderer.getFontAscenderSize(fontId) / 2;

      int startX = wordX;
      int strikeWidth = fullWordWidth;

      if (w.size() >= 3 && static_cast<uint8_t>(w[0]) == 0xE2 && static_cast<uint8_t>(w[1]) == 0x80 &&
          static_cast<uint8_t>(w[2]) == 0x83) {
        const char* visiblePtr = w.c_str() + 3;
        const int prefixWidth = renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", currentStyle);
        const int visibleWidth = renderer.getTextWidth(fontId, visiblePtr, currentStyle);
        startX = wordX + prefixWidth;
        strikeWidth = visibleWidth;
      }

      renderer.drawLine(startX, strikeY, startX + strikeWidth, strikeY, 3, true);
    }
  }
}

bool TextBlock::serialize(serialization::BufferedWriter& file) const {
  // Focus annotations are optional; vectors are either empty (no splits in this block)
  // or sized in lockstep with words[].
  const bool hasFocus = !wordFocusBoundary.empty();
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      (hasFocus && (words.size() != wordFocusBoundary.size() || words.size() != wordFocusSuffixX.size()))) {
    LOG_ERR("TXB", "Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u)\n",
            static_cast<uint32_t>(words.size()), static_cast<uint32_t>(wordXpos.size()),
            static_cast<uint32_t>(wordStyles.size()), static_cast<uint32_t>(wordFocusBoundary.size()),
            static_cast<uint32_t>(wordFocusSuffixX.size()));
    return false;
  }

  // Diagnostic only — deliberately does NOT return false. PageLine::serialize
  // has already written xPos/yPos by the time we run, and Section.cpp:115 does
  // not remove a partially-written file, so a mid-stream refusal would leave a
  // truncated cache that the fail-closed reader rejects — the same rebuild loop
  // this bound is meant to prevent. Log loudly and write anyway.
  for (const auto& w : words) {
    if (w.size() > MAX_SERIALIZED_WORD_BYTES) {
      LOG_ERR("TXB", "Word of %u bytes exceeds the %u-byte cache bound; this section will fail to reload",
              static_cast<uint32_t>(w.size()), MAX_SERIALIZED_WORD_BYTES);
      break;
    }
  }

  // Word data
  serialization::writePod(file, static_cast<uint16_t>(words.size()));
  for (const auto& w : words) serialization::writeString(file, w);
  for (auto x : wordXpos) serialization::writePod(file, x);
  for (auto s : wordStyles) serialization::writePod(file, s);
  // Focus block: 1-byte presence flag, followed by per-word vectors only when present.
  // Saves 3 bytes/word when focus reading is disabled or no word on this line was split.
  serialization::writePod(file, static_cast<uint8_t>(hasFocus ? 1 : 0));
  if (hasFocus) {
    for (auto b : wordFocusBoundary) serialization::writePod(file, b);
    for (auto sx : wordFocusSuffixX) serialization::writePod(file, sx);
  }

  // Style (alignment + margins/padding/indent)
  serialization::writePod(file, blockStyle.alignment);
  serialization::writePod(file, blockStyle.textAlignDefined);
  serialization::writePod(file, blockStyle.marginTop);
  serialization::writePod(file, blockStyle.marginBottom);
  serialization::writePod(file, blockStyle.marginLeft);
  serialization::writePod(file, blockStyle.marginRight);
  serialization::writePod(file, blockStyle.paddingTop);
  serialization::writePod(file, blockStyle.paddingBottom);
  serialization::writePod(file, blockStyle.paddingLeft);
  serialization::writePod(file, blockStyle.paddingRight);
  serialization::writePod(file, blockStyle.textIndent);
  serialization::writePod(file, blockStyle.textIndentDefined);
  serialization::writePod(file, blockStyle.isRtl);
  serialization::writePod(file, blockStyle.directionDefined);

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(serialization::BufferedReader& file) {
  uint16_t wc = 0;
  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;
  std::vector<EpdFontFamily::Style> wordStyles;
  std::vector<uint8_t> wordFocusBoundary;
  std::vector<uint16_t> wordFocusSuffixX;
  BlockStyle blockStyle;

  // Word count
  if (!serialization::readPod(file, wc)) {
    LOG_ERR("TXB", "Deserialization failed: could not read word count");
    return nullptr;
  }

  // A TextBlock is one rendered line. CrossInk uses the same 512-word cap;
  // values above it are corrupt and would otherwise drive large STL allocations.
  if (wc > MAX_WORDS_PER_TEXT_BLOCK) {
    LOG_ERR("TXB", "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }

  const size_t vectorBytes =
      static_cast<size_t>(wc) *
      (sizeof(std::string) + sizeof(int16_t) + sizeof(EpdFontFamily::Style) + sizeof(uint8_t) + sizeof(uint16_t));
  if (!heapguard::canAllocate(vectorBytes, 0)) {
    LOG_ERR("TXB", "Deserialization failed: insufficient heap for %u words", wc);
    return nullptr;
  }

  // Word data
  words.resize(wc);
  wordXpos.resize(wc);
  wordStyles.resize(wc);
  for (auto& w : words) {
    if (!readBoundedWord(file, w)) {
      LOG_ERR("TXB", "Deserialization failed: invalid word payload");
      return nullptr;
    }
  }
  for (auto& x : wordXpos) {
    if (!serialization::readPod(file, x)) {
      LOG_ERR("TXB", "Deserialization failed: truncated word positions");
      return nullptr;
    }
  }
  for (auto& s : wordStyles) {
    if (!serialization::readPod(file, s)) {
      LOG_ERR("TXB", "Deserialization failed: truncated word styles");
      return nullptr;
    }
  }
  // Focus block: presence flag, then vectors only if present. Empty vectors when absent
  // signal "no splits in this block" to render() (zero per-word RAM cost).
  uint8_t hasFocus = 0;
  if (!serialization::readPod(file, hasFocus) || hasFocus > 1) {
    LOG_ERR("TXB", "Deserialization failed: invalid focus metadata");
    return nullptr;
  }
  if (hasFocus) {
    wordFocusBoundary.resize(wc);
    wordFocusSuffixX.resize(wc);
    for (auto& b : wordFocusBoundary) {
      if (!serialization::readPod(file, b)) {
        LOG_ERR("TXB", "Deserialization failed: truncated focus boundaries");
        return nullptr;
      }
    }
    for (auto& sx : wordFocusSuffixX) {
      if (!serialization::readPod(file, sx)) {
        LOG_ERR("TXB", "Deserialization failed: truncated focus positions");
        return nullptr;
      }
    }
  }

  // Style (alignment + margins/padding/indent)
  if (!serialization::readPod(file, blockStyle.alignment) ||
      !serialization::readPod(file, blockStyle.textAlignDefined) ||
      !serialization::readPod(file, blockStyle.marginTop) || !serialization::readPod(file, blockStyle.marginBottom) ||
      !serialization::readPod(file, blockStyle.marginLeft) || !serialization::readPod(file, blockStyle.marginRight) ||
      !serialization::readPod(file, blockStyle.paddingTop) || !serialization::readPod(file, blockStyle.paddingBottom) ||
      !serialization::readPod(file, blockStyle.paddingLeft) || !serialization::readPod(file, blockStyle.paddingRight) ||
      !serialization::readPod(file, blockStyle.textIndent) ||
      !serialization::readPod(file, blockStyle.textIndentDefined) || !serialization::readPod(file, blockStyle.isRtl) ||
      !serialization::readPod(file, blockStyle.directionDefined)) {
    LOG_ERR("TXB", "Deserialization failed: truncated block style metadata");
    return nullptr;
  }

  auto* tb = new (std::nothrow) TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles),
                                          std::move(wordFocusBoundary), std::move(wordFocusSuffixX), blockStyle);
  if (!tb) {
    LOG_ERR("TXB", "OOM: TextBlock");
    return nullptr;
  }
  return std::unique_ptr<TextBlock>(tb);
}
