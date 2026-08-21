#include <Arduino.h>
#include <Serialization.h>

#include <memory>
#include <vector>

#include "Epub/Page.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/blocks/TextBlock.h"
#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"

namespace {
template <typename Serializer>
std::vector<uint8_t> serializePayload(Serializer&& serializer) {
  auto bytes = std::make_shared<std::vector<uint8_t>>();
  HalFile file = HalFile::forWrite(bytes);
  serialization::BufferedWriter writer(file);
  if (!serializer(writer) || !writer.flush()) return {};
  return *bytes;
}

template <typename Deserializer>
auto deserializePayload(const std::vector<uint8_t>& bytes, Deserializer&& deserializer) {
  auto shared = std::make_shared<std::vector<uint8_t>>(bytes);
  HalFile file = HalFile::forRead(shared);
  serialization::BufferedReader reader(file);
  return deserializer(reader);
}

std::shared_ptr<TextBlock> sampleTextBlock() {
  BlockStyle style;
  style.marginTop = 3;
  style.textAlignDefined = true;
  return std::make_shared<TextBlock>(std::vector<std::string>{"alpha", "beta"}, std::vector<int16_t>{4, 24},
                                     std::vector<EpdFontFamily::Style>{EpdFontFamily::REGULAR, EpdFontFamily::BOLD},
                                     std::vector<uint8_t>{2, 0}, std::vector<uint16_t>{7, 0}, style);
}

std::unique_ptr<TextBlock> deserializeText(const std::vector<uint8_t>& bytes) {
  return deserializePayload(bytes,
                            [](serialization::BufferedReader& reader) { return TextBlock::deserialize(reader); });
}

std::unique_ptr<ImageBlock> deserializeImage(const std::vector<uint8_t>& bytes) {
  return deserializePayload(bytes,
                            [](serialization::BufferedReader& reader) { return ImageBlock::deserialize(reader); });
}

std::unique_ptr<Page> deserializePage(const std::vector<uint8_t>& bytes) {
  return deserializePayload(bytes, [](serialization::BufferedReader& reader) { return Page::deserialize(reader); });
}
}  // namespace

TEST_CASE("section cache TextBlock round-trips and rejects every truncated prefix") {
  const auto original = sampleTextBlock();
  const auto bytes =
      serializePayload([&](serialization::BufferedWriter& writer) { return original->serialize(writer); });
  REQUIRE_FALSE(bytes.empty());

  auto decoded = deserializeText(bytes);
  REQUIRE(decoded);
  CHECK(decoded->getWords() == original->getWords());
  CHECK(decoded->getWordXpos() == original->getWordXpos());
  CHECK(decoded->getWordStyles() == original->getWordStyles());
  CHECK(decoded->getBlockStyle().marginTop == 3);
  CHECK(decoded->getBlockStyle().textAlignDefined);

  for (size_t length = 0; length < bytes.size(); length++) {
    INFO("truncated length=" << length);
    CHECK_FALSE(deserializeText(std::vector<uint8_t>(bytes.begin(), bytes.begin() + length)));
  }
}

TEST_CASE("section cache TextBlock rejects hostile counts, strings, flags, and low heap") {
  CHECK_FALSE(deserializeText({0x01, 0x02}));                          // 513 words, over the 512-word invariant.
  CHECK_FALSE(deserializeText({0x01, 0x00, 0x01, 0x04, 0x00, 0x00}));  // 1025-byte word.

  auto bytes =
      serializePayload([&](serialization::BufferedWriter& writer) { return sampleTextBlock()->serialize(writer); });
  REQUIRE(bytes.size() > 2);
  const auto savedFree = ESP.overrideFreeHeap;
  const auto savedLargest = ESP.overrideMaxAllocHeap;
  ESP.overrideFreeHeap = 0;
  ESP.overrideMaxAllocHeap = 0;
  CHECK_FALSE(deserializeText(bytes));
  ESP.overrideFreeHeap = savedFree;
  ESP.overrideMaxAllocHeap = savedLargest;
}

TEST_CASE("section cache TextBlock round-trips a word longer than CrossInk's 200-byte bound") {
  // At the original 200-byte bound this round-trip failed, causing a permanent rebuild loop
  // when layout produced legal long words (e.g. URLs).
  const std::string longWord(300, 'x');
  BlockStyle style;
  auto original = std::make_shared<TextBlock>(std::vector<std::string>{longWord}, std::vector<int16_t>{0},
                                              std::vector<EpdFontFamily::Style>{EpdFontFamily::REGULAR},
                                              std::vector<uint8_t>{0}, std::vector<uint16_t>{0}, style);

  const auto bytes =
      serializePayload([&](serialization::BufferedWriter& writer) { return original->serialize(writer); });
  REQUIRE_FALSE(bytes.empty());

  auto decoded = deserializeText(bytes);
  REQUIRE(decoded);
  REQUIRE(decoded->getWords().size() == 1);
  CHECK(decoded->getWords()[0] == original->getWords()[0]);
  CHECK(decoded->getWords() == original->getWords());
}

TEST_CASE("section cache ImageBlock round-trips and rejects every truncated prefix") {
  ImageBlock original("/books/cache/image.png", 123, 456);
  const auto bytes =
      serializePayload([&](serialization::BufferedWriter& writer) { return original.serialize(writer); });
  REQUIRE_FALSE(bytes.empty());

  auto decoded = deserializeImage(bytes);
  REQUIRE(decoded);
  CHECK(decoded->getImagePath() == original.getImagePath());
  CHECK(decoded->getWidth() == 123);
  CHECK(decoded->getHeight() == 456);

  for (size_t length = 0; length < bytes.size(); length++) {
    INFO("truncated length=" << length);
    CHECK_FALSE(deserializeImage(std::vector<uint8_t>(bytes.begin(), bytes.begin() + length)));
  }
}

TEST_CASE("section cache Page round-trips all element kinds and rejects truncation") {
  Page original;
  auto text = sampleTextBlock();
  original.elements.push_back(std::make_shared<PageLine>(text, 1, 2));
  original.elements.push_back(std::make_shared<PageImage>(std::make_shared<ImageBlock>("/img.png", 20, 30), 3, 4));
  original.elements.push_back(std::make_shared<PageHorizontalRule>(100, 2, 5, 6));

  TableFragmentCell cell;
  cell.isHeader = true;
  cell.lines.push_back(text);
  TableFragmentRow row;
  row.height = 12;
  row.headerSeparator = true;
  row.cells.push_back(cell);
  original.elements.push_back(
      std::make_shared<PageTableFragment>(100, 1, 2, 10, std::vector<TableFragmentRow>{row}, 7, 8));
  original.addFootnote("1", "chapter.xhtml#note-1");

  const auto bytes =
      serializePayload([&](serialization::BufferedWriter& writer) { return original.serialize(writer); });
  REQUIRE_FALSE(bytes.empty());
  auto decoded = deserializePage(bytes);
  REQUIRE(decoded);
  REQUIRE(decoded->elements.size() == 4);
  CHECK(decoded->elements[0]->getTag() == TAG_PageLine);
  CHECK(decoded->elements[1]->getTag() == TAG_PageImage);
  CHECK(decoded->elements[2]->getTag() == TAG_PageHorizontalRule);
  CHECK(decoded->elements[3]->getTag() == TAG_PageTableFragment);
  REQUIRE(decoded->footnotes.size() == 1);
  CHECK(std::string(decoded->footnotes[0].href) == "chapter.xhtml#note-1");

  for (size_t length = 0; length < bytes.size(); length++) {
    INFO("truncated length=" << length);
    CHECK_FALSE(deserializePage(std::vector<uint8_t>(bytes.begin(), bytes.begin() + length)));
  }
}

TEST_CASE("section cache Page rejects missing tags, unknown tags, oversized counts, and low heap") {
  CHECK_FALSE(deserializePage({0x01, 0x00}));
  CHECK_FALSE(deserializePage({0x01, 0x00, 0xFF}));
  CHECK_FALSE(deserializePage({0x01, 0x04}));  // 1025 elements.

  Page empty;
  const auto bytes = serializePayload([&](serialization::BufferedWriter& writer) { return empty.serialize(writer); });
  REQUIRE_FALSE(bytes.empty());
  auto nonEmpty = bytes;
  nonEmpty[0] = 1;
  nonEmpty[1] = 0;
  nonEmpty.insert(nonEmpty.begin() + 2, static_cast<uint8_t>(TAG_PageHorizontalRule));
  nonEmpty.insert(nonEmpty.begin() + 3, {0, 0, 0, 0, 1, 0, 1});

  const auto savedFree = ESP.overrideFreeHeap;
  const auto savedLargest = ESP.overrideMaxAllocHeap;
  ESP.overrideFreeHeap = 0;
  ESP.overrideMaxAllocHeap = 0;
  CHECK_FALSE(deserializePage(nonEmpty));
  ESP.overrideFreeHeap = savedFree;
  ESP.overrideMaxAllocHeap = savedLargest;
}
