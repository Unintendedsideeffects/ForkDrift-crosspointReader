#pragma once

#include <string_view>

/**
 * Decides what an EPUB `<guide><reference type=... href=...>` entry means.
 *
 * Split out of ContentOpfParser purely so this decision table is host-testable:
 * the parser itself drags in expat, HalFile and the book metadata cache, while
 * the rule below is what actually determines where a book opens.
 */
namespace guide_reference {

enum class Slot {
  Ignore,
  StartLocation,  // first page the reader should open at
  CoverPage,      // cover page to skip past / render as the cover
};

/**
 * @param type            the reference's `type` attribute, verbatim
 * @param haveStartLocation whether a start location has already been recorded
 * @param haveCoverPage     whether a cover page has already been recorded
 *
 * Only `start` designates a reading location. EPUB 2 guides routinely tag
 * *every* content file as `type="text"`, so that type identifies no particular
 * location -- honouring it opened the book at whichever `text` reference
 * happened to come last. With no `start` present the reader falls back to spine
 * index 0, which is correct far more often than an arbitrary `text` entry.
 *
 * Both slots are first-wins: a guide may repeat a type, and the earliest entry
 * is the one the publisher meant.
 */
constexpr Slot classify(std::string_view type, bool haveStartLocation, bool haveCoverPage) {
  if (type == "start") {
    return haveStartLocation ? Slot::Ignore : Slot::StartLocation;
  }
  if (type == "cover" || type == "cover-page") {
    return haveCoverPage ? Slot::Ignore : Slot::CoverPage;
  }
  return Slot::Ignore;
}

}  // namespace guide_reference
