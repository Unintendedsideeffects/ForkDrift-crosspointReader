#include "doctest/doctest.h"
#include "lib/Markdown/MarkdownAST.h"
#include "lib/Markdown/MarkdownPreprocessor.h"
#include "lib/Markdown/MarkdownParser.h"

#include <string>

namespace {

bool containsNodeType(const MdNode* node, const MdNodeType type) {
  if (node == nullptr) {
    return false;
  }
  if (node->type == type) {
    return true;
  }
  for (const auto& child : node->children) {
    if (containsNodeType(child.get(), type)) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("testMarkdownLimits") {
  MarkdownParser parser;
  std::string largeInput(MarkdownParser::MAX_INPUT_SIZE + 1, 'a');
  auto resultLarge = parser.parse(largeInput);
  CHECK(resultLarge == nullptr);
  std::string deepNesting = "";
  for (int i = 0; i < 60; ++i) {
    deepNesting += "> ";
  }
  deepNesting += "Deep";
  auto resultDeep = parser.parse(deepNesting);
  CHECK(resultDeep == nullptr);
  std::string manyNodes = "";
  for (int i = 0; i < 6000; ++i) {
    manyNodes += "p\n\n";
  }
  auto resultMany = parser.parse(manyNodes);
  CHECK(resultMany == nullptr);
}

TEST_CASE("obsidianPreprocessorKeepsFirmwareMdFeaturesAligned") {
  const std::string input =
      "---\n"
      "title: Demo\n"
      "---\n"
      "%% hidden comment %%\n"
      "# Heading {#custom-id}\n"
      "> [!note]- Folded title\n"
      "[[Target Note|Alias]]\n"
      "![[image.png|120x80]]\n"
      "Task item ^task-block\n"
      "Term\n"
      ": definition\n"
      "[Table caption]\n"
      "| A | B |\n"
      "| - | - |\n"
      "| 1 | 2 |\n"
      "Tag #project and ==highlight== with ~sub~ and ^sup^\n";

  const std::string output = markdown::preprocess::preprocessDocument(input);

  CHECK(output.find("title: Demo") == std::string::npos);
  CHECK(output.find("hidden comment") == std::string::npos);
  CHECK(output.find("# Heading") != std::string::npos);
  CHECK(output.find("{#custom-id}") == std::string::npos);
  CHECK(output.find("> **note** [-] Folded title") != std::string::npos);
  CHECK(output.find("[Alias](<Target Note>)") != std::string::npos);
  CHECK(output.find("![image](image.png \"120x80\")") != std::string::npos);
  CHECK(output.find("Task item ^task-block") == std::string::npos);
  CHECK(output.find("Task item") != std::string::npos);
  CHECK(output.find("**Term**") != std::string::npos);
  CHECK(output.find("- definition") != std::string::npos);
  CHECK(output.find("*Table caption*") != std::string::npos);
  CHECK(output.find("*#project*") != std::string::npos);
  CHECK(output.find("<mark>highlight</mark>") != std::string::npos);
  CHECK(output.find("<sub>sub</sub>") != std::string::npos);
  CHECK(output.find("<sup>sup</sup>") != std::string::npos);
}

TEST_CASE("obsidianPreprocessorSkipsExpansionInsideCodeFences") {
  const std::string input =
      "Before ![[Note]]\n"
      "```md\n"
      "![[Inside Fence]]\n"
      "```\n";

  const std::string output = markdown::preprocess::preprocessDocument(input);

  CHECK(output.find("[Note](Note)") != std::string::npos);
  CHECK(output.find("![[Inside Fence]]") != std::string::npos);
}

TEST_CASE("parseWithPreprocessingUsesSharedObsidianTransforms") {
  MarkdownParser parser;
  const std::string input =
      "---\n"
      "title: Demo\n"
      "---\n"
      "%% hidden %%\n"
      "# Heading {#custom-id}\n"
      "> [!tip] Useful\n"
      "[[Target Note|Alias]]\n";

  auto ast = parser.parseWithPreprocessing(input);
  REQUIRE(ast != nullptr);
  CHECK(ast->getPlainText().find("Heading") != std::string::npos);
  CHECK(ast->getPlainText().find("Alias") != std::string::npos);
  CHECK(containsNodeType(ast.get(), MdNodeType::Heading));
  CHECK(containsNodeType(ast.get(), MdNodeType::Link));
}
