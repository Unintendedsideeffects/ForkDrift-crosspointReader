#pragma once
#include <Epub.h>
#include <GfxRenderer.h>

#include <memory>
#include <string>

#include "CrossPointPosition.h"

/**
 * Progress position representation.
 */
struct SavedProgressPosition {
  std::string xpath;  // XPath-like progress string
  float percentage;   // Progress percentage (0.0 to 1.0)
};

/**
 * Maps between CrossPoint and SavedProgress position formats, such as those used by KOReader.
 *
 * CrossPoint tracks position as (spineIndex, pageNumber).
 * SavedProgress uses XPath-like strings + percentage.
 *
 * Outgoing positions retain the chapter's HTML ancestry. If structural
 * resolution fails, xpath is left empty and must not be uploaded.
 */
class ProgressMapper {
 public:
  /**
   * Convert CrossPoint position to SavedProgress format.
   *
   * @param epub The EPUB book
   * @param pos CrossPoint position
   * @return SavedProgress position
   */
  static SavedProgressPosition toSavedProgress(const std::shared_ptr<Epub>& epub, const CrossPointPosition& pos);

  /**
   * Convert SavedProgress position to CrossPoint format.
   *
   * Note: The returned pageNumber may be approximate since different
   * rendering settings produce different page counts.
   *
   * @param epub The EPUB book
   * @param savedPos SavedProgress position
   * @param renderer GfxRenderer for page count estimation
   * @param currentSpineIndex Index of the currently open spine item (for density estimation)
   * @param totalPagesInCurrentSpine Total pages in the current spine item (for density estimation)
   * @return CrossPoint position
   */
  static CrossPointPosition toCrossPoint(const std::shared_ptr<Epub>& epub, const SavedProgressPosition& savedPos,
                                         GfxRenderer& renderer, int currentSpineIndex = -1,
                                         int totalPagesInCurrentSpine = 0, int fallbackTotalPages = 0);
};
