#include <expat.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ChapterXPathResolver.h"
#include "Epub/VisibleTextUtils.h"
#include "Utf8.h"

namespace {
std::shared_ptr<Epub> epubWith(std::string xhtml) {
  std::vector<std::string> spine;
  spine.push_back(std::move(xhtml));
  return std::make_shared<Epub>(std::move(spine));
}

constexpr char kNestedFixture[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><body><div><section><p>Alpha bravo</p><p>Second <em>nested</em> tail</p></section></div></body></html>)";
constexpr char kNonVisibleInlineFixture[] =
    R"(<html><body><p><RP><span>hidden</span></RP>Visible text</p></body></html>)";
constexpr char kCommentBoundaryFixture[] = R"(<html><body><p>before<!--comment-->after</p></body></html>)";
constexpr char kProcessingInstructionBoundaryFixture[] = R"(<html><body><p>before<?marker?>after</p></body></html>)";
constexpr char kCdataBoundaryFixture[] = R"(<html><body><p>before<![CDATA[middle]]>after</p></body></html>)";
constexpr char kHiddenCdataFixture[] = R"(<html><body><p>before<rp><![CDATA[hidden]]></rp>after</p></body></html>)";
}  // namespace

TEST(KOReaderXPathResolver, ResolvesExactOffsetWithFullAncestry) {
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epubWith(kNestedFixture), 0, 6),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[1]/text()[1].6");
}

TEST(KOReaderXPathResolver, PreservesNestedInlineTextNode) {
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epubWith(kNestedFixture), 0, 26),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[2]/text()[2].2");
}

TEST(KOReaderXPathResolver, EmitsDetailedAnchorForOffsetZero) {
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epubWith(kNestedFixture), 0, 0),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, IgnoresNestedNonVisibleInlineText) {
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epubWith(kNonVisibleInlineFixture), 0, 0),
            "/body/DocFragment[1]/body/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, ResolvesProgressAfterNestedNonVisibleInlineText) {
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epubWith(kNonVisibleInlineFixture), 0, 1.0f),
            "/body/DocFragment[1]/body/p[1]/text()[1].12");
}

TEST(KOReaderXPathResolver, CountsUtf8CodepointsInsteadOfBytes) {
  const auto epub = epubWith(
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?><html><body><p>A\xC3\xA9\xE4\xB8\xAD"
      "B</p></body></html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 3),
            "/body/DocFragment[1]/body/p[1]/text()[1].3");
}

TEST(KOReaderXPathResolver, SplitsTextNodesAroundComments) {
  const auto epub = epubWith(kCommentBoundaryFixture);
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 6),
            "/body/DocFragment[1]/body/p[1]/text()[2].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 11).empty());
}

TEST(KOReaderXPathResolver, SplitsTextNodesAroundProcessingInstructions) {
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epubWith(kProcessingInstructionBoundaryFixture), 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
}

TEST(KOReaderXPathResolver, SplitsTextNodesAroundCdata) {
  const auto epub = epubWith(kCdataBoundaryFixture);
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 13),
            "/body/DocFragment[1]/body/p[1]/text()[3].1");
}

TEST(KOReaderXPathResolver, CountsVisibleCdataAndIgnoresHiddenCdata) {
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epubWith(kHiddenCdataFixture), 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
}

TEST(KOReaderXPathResolver, ResolvesDirectBodyTextNode) {
  EXPECT_EQ(
      ChapterXPathResolver::findXPathForVisibleTextOffset(epubWith("<html><body>Direct text</body></html>"), 0, 0),
      "/body/DocFragment[1]/body/text()[1].0");
}

TEST(KOReaderXPathResolver, ReturnsEmptyForUnusableContent) {
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(epubWith(""), 0, 0).empty());
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(epubWith("<html><body><p>broken"), 0, 100).empty());
}

TEST(KOReaderXPathResolver, KeepsParagraphOnlyResolutionUnchanged) {
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epubWith(kNestedFixture), 0, 2),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[2]");
}

namespace {
constexpr char kSplitFixture[] =
    "<html><body>\n\n <section>\n\n <p>Alpha</p>\n\n <p>Bravo</p>\n\n </section>\n</body></html>";
}  // namespace

TEST(KOReaderXPathResolver, SplitStartSkipsBodyAndContainerWhitespace) {
  const auto epub = epubWith(kSplitFixture);
  for (uint32_t offset = 0; offset <= 6; ++offset) {
    EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, offset, 2),
              "/body/DocFragment[1]/body/section[1]/p[1]/text()[1].0")
        << offset;
  }
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.0f, 2),
            "/body/DocFragment[1]/body/section[1]/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, SplitInteriorRetainsExactOffset) {
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epubWith(kSplitFixture), 0, 8, 2),
            "/body/DocFragment[1]/body/section[1]/p[1]/text()[1].2");
}

TEST(KOReaderXPathResolver, SplitGapAdvancesToNextChild) {
  const auto epub = epubWith(kSplitFixture);
  for (uint32_t offset = 11; offset <= 14; ++offset) {
    EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, offset, 2),
              "/body/DocFragment[1]/body/section[1]/p[2]/text()[1].0")
        << offset;
  }
}

TEST(KOReaderXPathResolver, SplitEndUsesLastContentCharacter) {
  const auto epub = epubWith(kSplitFixture);
  for (uint32_t offset = 19; offset <= 23; ++offset) {
    EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, offset, 2),
              "/body/DocFragment[1]/body/section[1]/p[2]/text()[1].4")
        << offset;
  }
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f, 2),
            "/body/DocFragment[1]/body/section[1]/p[2]/text()[1].5");
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 100, 2).empty());
}

TEST(KOReaderXPathResolver, SplitWithoutMappedContentDoesNotInventAnchor) {
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(
                  epubWith("<html><body>\n<section>\n</section></body></html>"), 0, 0, 2)
                  .empty());
}

TEST(KOReaderXPathResolver, SplitDirectChildrenAndNestedInlineText) {
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(
                epubWith("<html><body>\n<p><em>Hello</em></p></body></html>"), 0, 0, 1),
            "/body/DocFragment[1]/body/p[1]/em[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, ResolvesDivSpanTextAfterEmptyParagraph) {
  const auto epub = epubWith("<html><body><p id='anchor'/><div><span>Alpha</span> tail</div>End</body></html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 2),
            "/body/DocFragment[1]/body/div[1]/span[1]/text()[1].2");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 6),
            "/body/DocFragment[1]/body/div[1]/text()[1].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 11), "/body/DocFragment[1]/body/text()[1].1");
}

TEST(KOReaderXPathResolver, ResumesParentTextAfterNestedElements) {
  const auto epub = epubWith("<html><body>One<div>Two<span>Three</span>Four</div>Five</body></html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 12),
            "/body/DocFragment[1]/body/div[1]/text()[2].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 16), "/body/DocFragment[1]/body/text()[2].1");
}

TEST(KOReaderXPathResolver, SkipsWhitespaceAnchorsWithoutRenumberingNodes) {
  const auto epub = epubWith("<html><body> \n<div> \t<span>Alpha</span> \n<em/>Bravo</div> \n</body></html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
            "/body/DocFragment[1]/body/div[1]/span[1]/text()[1].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 9),
            "/body/DocFragment[1]/body/div[1]/text()[3].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "/body/DocFragment[1]/body/div[1]/text()[3].5");
}

TEST(KOReaderXPathResolver, TrimsProgressEndpointAcrossStreamChunkSizes) {
  const auto epub = epubWith("<html><body><div>Alpha\xC3\xA9 \t\n</div> \n</body></html>");
  for (const size_t chunk : {1, 2, 7, 1024}) {
    SCOPED_TRACE(chunk);
    epub->streamChunkSize = chunk;
    EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f),
              "/body/DocFragment[1]/body/div[1]/text()[1].6");
  }
}

TEST(KOReaderXPathResolver, PreservesLongTextNodeAcrossChunkAndUtf8Boundaries) {
  const auto epub =
      epubWith("<html><body><div>" + std::string(1007, 'a') + "\xC3\xA9<span>X</span>Tail</div></body></html>");
  for (const size_t chunk : {1, 7, 1024}) {
    SCOPED_TRACE(chunk);
    epub->streamChunkSize = chunk;
    EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 1007),
              "/body/DocFragment[1]/body/div[1]/text()[1].1007");
    EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 1010),
              "/body/DocFragment[1]/body/div[1]/text()[2].1");
  }
}

TEST(KOReaderXPathResolver, WhitespaceOnlyChapterDoesNotInventAnchor) {
  const auto epub = epubWith("<html><body> \n<div> \t</div></body></html>");
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0).empty());
  EXPECT_TRUE(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f).empty());
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0, 1).empty());
}

TEST(KOReaderXPathResolver, PreservesNonAsciiSpacesAsContent) {
  const auto epub = epubWith("<html><body><div>\xC2\xA0</div></body></html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
            "/body/DocFragment[1]/body/div[1]/text()[1].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "/body/DocFragment[1]/body/div[1]/text()[1].1");
}

TEST(KOReaderXPathResolver, SplitFallbackSkipsWhitespaceOnlyMappedChildren) {
  const auto epub = epubWith("<html><body><section><div>Alpha \t</div><div> \n</div> </section></body></html>");
  for (const size_t chunk : {1, 7, 1024}) {
    SCOPED_TRACE(chunk);
    epub->streamChunkSize = chunk;
    EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 9, 2),
              "/body/DocFragment[1]/body/section[1]/div[1]/text()[1].4");
    EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f, 2),
              "/body/DocFragment[1]/body/section[1]/div[1]/text()[1].5");
  }
}

TEST(KOReaderXPathResolver, IgnoresNonVisibleTagsOutsideParagraphs) {
  const auto epub = epubWith("<html><body><div>One<STYLE>hidden</STYLE><rt>hidden</rt>Two</div></body></html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 4),
            "/body/DocFragment[1]/body/div[1]/text()[2].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "/body/DocFragment[1]/body/div[1]/text()[2].3");
}

namespace {
struct ParserOffsetProbe {
  XML_Parser parser = nullptr;
  bool insideBody = false;
  int depth = 0;
  int bodyDepth = -1;
  int nonVisibleDepth = 0;
  uint32_t visibleChars = 0;

  ParserOffsetProbe() {
    parser = XML_ParserCreate(nullptr);
    if (!parser) return;
    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, &ParserOffsetProbe::startElement, &ParserOffsetProbe::endElement);
    XML_SetCharacterDataHandler(parser, &ParserOffsetProbe::characterData);
  }

  ~ParserOffsetProbe() {
    if (parser) XML_ParserFree(parser);
  }

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char**) {
    auto* self = static_cast<ParserOffsetProbe*>(userData);
    if (!self->insideBody) {
      if (VisibleTextUtils::equalsTag(name, "body")) {
        self->insideBody = true;
        self->bodyDepth = self->depth;
      }
      self->depth++;
      return;
    }
    if (self->nonVisibleDepth > 0 || VisibleTextUtils::isNonVisibleElement(name)) self->nonVisibleDepth++;
    self->depth++;
  }

  static void XMLCALL endElement(void* userData, const XML_Char* name) {
    auto* self = static_cast<ParserOffsetProbe*>(userData);
    self->depth--;
    if (!self->insideBody) return;
    if (self->depth == self->bodyDepth && VisibleTextUtils::equalsTag(name, "body")) {
      self->insideBody = false;
      self->nonVisibleDepth = 0;
      return;
    }
    if (self->nonVisibleDepth > 0) self->nonVisibleDepth--;
  }

  static void XMLCALL characterData(void* userData, const XML_Char* data, const int len) {
    auto* self = static_cast<ParserOffsetProbe*>(userData);
    if (!self->insideBody || self->nonVisibleDepth > 0 || len <= 0) return;
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(data);
    const unsigned char* end = ptr + len;
    while (ptr < end) {
      utf8NextCodepoint(&ptr);
      self->visibleChars++;
    }
  }

  uint32_t count(const char* xhtml) {
    if (!parser) return 0;
    const int n = static_cast<int>(std::char_traits<char>::length(xhtml));
    if (XML_Parse(parser, xhtml, n, XML_TRUE) != XML_STATUS_OK) return 0;
    return visibleChars;
  }
};
}  // namespace

TEST(KOReaderXPathResolver, ParserOffsetProbeAgreesOnRubyRtAndEntities) {
  constexpr char kFixture[] =
      "<html><head><title>skip</title></head><body><p>ab<ruby>漢<rt>kan</rt></ruby>cd&amp;x</p>"
      "<style>no</style><script>no</script></body></html>";
  ParserOffsetProbe probe;
  const uint32_t counted = probe.count(kFixture);
  EXPECT_EQ(counted, 7u);
  const auto epub = epubWith(kFixture);
  const std::string atHan = ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 2);
  const std::string afterRuby = ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 3);
  EXPECT_NE(atHan.find("ruby"), std::string::npos);
  EXPECT_EQ(afterRuby.find("rt"), std::string::npos);
  EXPECT_FALSE(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, counted - 1).empty());
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, counted).empty());
}

TEST(KOReaderXPathResolver, WhitespaceTargetsAreIndependentOfStreamChunkSize) {
  const auto epub = epubWith("<html><body><div>  Alpha   </div><p>Bravo</p></body></html>");
  const auto internal = epubWith("<html><body><div>Alpha   Bravo</div></body></html>");
  for (const size_t chunk : {1, 2, 7, 1024}) {
    SCOPED_TRACE(chunk);
    epub->streamChunkSize = chunk;
    internal->streamChunkSize = chunk;
    EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
              "/body/DocFragment[1]/body/div[1]/text()[1].2");
    EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 8),
              "/body/DocFragment[1]/body/p[1]/text()[1].0");
    EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(internal, 0, 6),
              "/body/DocFragment[1]/body/div[1]/text()[1].8");
    EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.5f), "/body/DocFragment[1]/body/p[1]/text()[1].0");
  }
}
