#pragma once

#include <HalStorage.h>

#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "CssStyle.h"

/**
 * Lightweight CSS parser for EPUB stylesheets
 *
 * Parses CSS files and extracts styling information relevant for e-ink display.
 * Uses a two-phase approach: first tokenizes the CSS content, then builds
 * a rule database that can be queried during HTML parsing.
 *
 * Supported selectors:
 *   - Element selectors: p, div, h1, etc.
 *   - Class selectors: .classname
 *   - Combined: element.classname
 *   - Grouped: selector1, selector2 { }
 *   - Two-part descendant: ancestor subject (e.g. "div p", "section.chapter p")
 *
 * Not supported (silently ignored):
 *   - Three-or-more-part descendant selectors
 *   - Child/sibling combinators (>, +, ~)
 *   - Pseudo-classes and pseudo-elements
 *   - Media queries (content is skipped)
 *   - @import, @font-face, etc.
 */

/**
 * Represents one open ancestor element in the HTML parse tree.
 * The `depth` field is used by ChapterHtmlSlimParser for stack management;
 * CssParser only reads `tag` and `classAttr` for selector matching.
 */
struct CssAncestorEntry {
  int depth = 0;
  std::string tag;
  std::string classAttr;
};

class CssParser {
 public:
  // Bump when CSS cache format or rules change; section caches are invalidated when this changes
  // v7: textDecoration became combinable bit flags (v6); direction (RTL) added.
  // 8: rules defining no supported property are no longer stored, so a v7 cache
  // would carry entries this build would never have written.
  static constexpr uint8_t CSS_CACHE_VERSION = 8;

  static constexpr size_t MAX_DESCENDANT_RULES = 100;

  CssParser() = default;
  explicit CssParser(const std::string& cacheDir);
  ~CssParser() = default;

  // Non-copyable
  CssParser(const CssParser&) = delete;
  CssParser& operator=(const CssParser&) = delete;

  /**
   * Load and parse CSS from a file stream.
   * Can be called multiple times to accumulate rules from multiple stylesheets.
   * @param source Open file handle to read from
   * @return true if parsing completed (even if no rules found)
   */
  bool loadFromStream(HalFile& source);

  /**
   * Look up the style for an HTML element, considering tag name, class attributes, and ancestors.
   * Applies CSS cascade: element style < descendant rules < class style < element.class style
   *
   * @param tagName The HTML element name (e.g., "p", "div")
   * @param classAttr The class attribute value (may contain multiple space-separated classes)
   * @param ancestors Open ancestor elements in the parse tree, innermost last
   * @return Combined style with all applicable rules merged
   */
  [[nodiscard]] CssStyle resolveStyle(const std::string& tagName, const std::string& classAttr,
                                      const std::vector<CssAncestorEntry>& ancestors = {}) const;

  /**
   * Parse an inline style attribute string.
   * @param styleValue The value of a style="" attribute
   * @return Parsed style properties
   */
  [[nodiscard]] static CssStyle parseInlineStyle(std::string_view styleValue);

  /**
   * Check if any rules have been loaded
   */
  [[nodiscard]] bool empty() const { return rulesBySelector_.empty(); }

  /**
   * Get count of loaded rule sets
   */
  [[nodiscard]] size_t ruleCount() const { return rulesBySelector_.size(); }

  /**
   * Clear all loaded rules
   */
  void clear() {
    rulesBySelector_.clear();
    descendantRules_.clear();
  }

  /**
   * Hand the rule storage back to the heap.
   *
   * clear() destroys the elements but leaves the bucket array and the vector's
   * capacity allocated, so it does not actually reduce residency. Swapping
   * against empty containers is what frees them. Use this when the rules are
   * not needed again until the next section build, which reloads from cache.
   */
  void releaseMemory() {
    std::unordered_map<std::string, CssStyle>().swap(rulesBySelector_);
    std::vector<DescendantRule>().swap(descendantRules_);
  }

  /**
   * Save parsed CSS rules to a cache file.
   * @param file Open file handle to write to
   * @return true if cache was written successfully
   */
  bool saveToCache(HalFile& file) const;

  /**
   * Load CSS rules from a cache file.
   * Clears any existing rules before loading.
   * @param file Open file handle to read from
   * @return true if cache was loaded successfully
   */
  bool loadFromCache(HalFile& file);

  // Compatibility helpers for callers that want parser-owned cache IO.
  [[nodiscard]] bool hasCache() const;
  bool saveToCache() const;
  bool loadFromCache();

 private:
  struct DescendantRule {
    std::string ancestorSelector;  // e.g. "div", ".chapter", "section.body"
    std::string subjectSelector;   // e.g. "p", ".indent", "p.indent"
    CssStyle style;
  };

  // Storage: maps normalized selector -> style properties
  std::unordered_map<std::string, CssStyle> rulesBySelector_;
  std::vector<DescendantRule> descendantRules_;
  std::string cacheDir_;

  // Internal parsing helpers
  void processRuleBlockWithStyle(std::string_view selectorGroup, const CssStyle& style);
  // Pre-flight heap check before the rule containers grow. Must be checked
  // *before* inserting: with -fno-exceptions a failed container growth aborts.
  [[nodiscard]] bool canGrowRuleContainers() const;
  static bool selectorMatchesElement(const std::string& selector, const std::string& tag, const std::string& classAttr);
  static CssStyle parseDeclarations(std::string_view declBlock);
  static void parseDeclarationIntoStyle(std::string_view decl, CssStyle& style);

  // Individual property value parsers
  static CssTextAlign interpretAlignment(std::string_view val);
  static CssFontStyle interpretFontStyle(std::string_view val);
  static CssFontWeight interpretFontWeight(std::string_view val);
  static CssTextDecoration interpretDecoration(const std::string& val);
  static CssLength interpretLength(std::string_view val);
  static bool tryInterpretLength(std::string_view val, CssLength& out);

  // String utilities
  static std::string normalized(std::string_view s);
  static std::vector<std::string> splitWhitespace(std::string_view s);

  void deleteCache() const;
};
