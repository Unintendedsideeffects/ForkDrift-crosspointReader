#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MarkdownAST.h"
#include "MarkdownNavigation.h"

class Markdown {
 public:
  Markdown(std::string path, std::string cacheBasePath);

  bool load();
  const std::string& getPath() const { return filepath; }
  const std::string& getCachePath() const { return cachePath; }
  size_t getFileSize() const { return fileSize; }
  std::string getTitle() const;
  void setupCacheDir() const;
  std::string getHtmlPath() const;
  std::string getContentBasePath() const;

  // Legacy HTML-based pipeline
  bool ensureHtml();

  // AST-based pipeline
  bool parseToAst();
  bool hasAst() const { return ast != nullptr; }
  const MdNode* getAst() const { return ast.get(); }
  MarkdownNavigation* getNavigation() { return navigation.get(); }
  const MarkdownNavigation* getNavigation() const { return navigation.get(); }

  // Get raw content (for AST parsing)
  std::string getContent() const;

 private:
  std::string filepath;
  std::string cacheBasePath;
  std::string cachePath;
  size_t fileSize = 0;
  bool loaded = false;

  // AST data
  std::unique_ptr<MdNode> ast;
  std::unique_ptr<MarkdownNavigation> navigation;

  bool renderToHtmlFile(const std::string& htmlPath) const;
  std::string preprocessContent(std::string content, int depth, std::vector<std::string>& stack) const;
};
