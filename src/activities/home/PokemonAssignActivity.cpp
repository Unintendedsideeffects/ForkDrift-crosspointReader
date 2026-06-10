#include "activities/home/PokemonAssignActivity.h"

#if ENABLE_POKEMON_PARTY

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <memory>

#include "MappedInputManager.h"
#include "activities/util/ListPickerActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/PokemonBookDataStore.h"
#include "util/PokemonSpriteCache.h"
#include "util/PokemonTeamStore.h"
#include "util/RecentBooksStore.h"

namespace {
constexpr int kMaxBooks = 6;  // the party is the six most-recent books

std::string prettyName(const char* raw) {
  std::string out = raw ? raw : "";
  if (out.empty()) {
    return "?";
  }
  out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

void ensureSpritesForMember(JsonVariantConst member) {
  const int speciesId = member["speciesId"] | member["id"] | 0;
  if (speciesId > 0) {
    PokemonSpriteCache::ensureSpriteById(speciesId, PokemonSpriteCache::kDefaultSpriteSize,
                                         PokemonSpriteCache::kDefaultSpriteSize);
  }
  for (JsonVariantConst stage : member["evolutionChain"].as<JsonArrayConst>()) {
    const int stageId = stage["speciesId"] | 0;
    if (stageId > 0) {
      PokemonSpriteCache::ensureSpriteById(stageId, PokemonSpriteCache::kDefaultSpriteSize,
                                           PokemonSpriteCache::kDefaultSpriteSize);
    }
  }
}
}  // namespace

void PokemonAssignActivity::onEnter() {
  Activity::onEnter();

  // Recent books = the party (up to six most recent).
  const auto& recent = RECENT_BOOKS.getBooks();
  const int count = std::min(static_cast<int>(recent.size()), kMaxBooks);
  books_.clear();
  books_.reserve(count);
  for (int i = 0; i < count; i++) {
    books_.push_back({recent[i].path, recent[i].title.empty() ? recent[i].path : recent[i].title});
  }

  // Prebaked team roster (no network needed — built earlier over the web plugin).
  teamNames_.clear();
  if (PokemonTeamStore::loadTeamDocument(teamDoc_) && teamDoc_["team"].is<JsonArrayConst>()) {
    for (JsonVariantConst member : teamDoc_["team"].as<JsonArrayConst>()) {
      teamNames_.push_back(prettyName(member["name"] | ""));
    }
  }

  if (teamNames_.empty() || books_.empty()) {
    phase_ = Phase::NoTeam;
    statusMessage_ = teamNames_.empty() ? std::string(tr(STR_PARTY_NO_TEAM)) : std::string(tr(STR_PARTY_EMPTY));
  } else {
    phase_ = Phase::LaunchBookPicker;
  }
  requestUpdate();
}

void PokemonAssignActivity::loop() {
  if (phase_ == Phase::LaunchBookPicker) {
    // Launch from loop (not onEnter) so the parent has fully entered first.
    phase_ = Phase::AwaitingPicker;
    launchBookPicker();
    return;
  }
  if (phase_ == Phase::NoTeam || phase_ == Phase::Done) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
  }
}

void PokemonAssignActivity::launchBookPicker() {
  std::vector<std::string> titles;
  titles.reserve(books_.size());
  for (const auto& book : books_) {
    titles.push_back(book.title);
  }
  startActivityForResult(
      std::make_unique<ListPickerActivity>(renderer, mappedInput, StrId::STR_PARTY_PICK_BOOK, std::move(titles), 0),
      [this](const ActivityResult& r) {
        if (r.isCancelled || !std::holds_alternative<ListPickerResult>(r.data)) {
          finish();
          return;
        }
        const int idx = std::get<ListPickerResult>(r.data).selectedIndex;
        if (idx < 0 || idx >= static_cast<int>(books_.size())) {
          finish();
          return;
        }
        chosenBookIndex_ = idx;
        launchPokemonPicker();
      });
}

void PokemonAssignActivity::launchPokemonPicker() {
  std::vector<std::string> names = teamNames_;  // ListPicker takes ownership
  startActivityForResult(
      std::make_unique<ListPickerActivity>(renderer, mappedInput, StrId::STR_PARTY_PICK_POKEMON, std::move(names), 0),
      [this](const ActivityResult& r) {
        if (r.isCancelled || !std::holds_alternative<ListPickerResult>(r.data)) {
          // Back from the member picker returns to the book picker.
          phase_ = Phase::LaunchBookPicker;
          requestUpdate();
          return;
        }
        writeAssignment(std::get<ListPickerResult>(r.data).selectedIndex);
      });
}

void PokemonAssignActivity::writeAssignment(int memberIndex) {
  if (memberIndex < 0 || memberIndex >= static_cast<int>(teamNames_.size()) || chosenBookIndex_ < 0 ||
      chosenBookIndex_ >= static_cast<int>(books_.size())) {
    phase_ = Phase::Done;
    statusMessage_ = std::string(tr(STR_PARTY_SAVE_FAILED));
    requestUpdate();
    return;
  }

  phase_ = Phase::Done;
  statusMessage_ = std::string(tr(STR_PARTY_CACHING_SPRITES));
  requestUpdateAndWait();

  const std::string& bookPath = books_[chosenBookIndex_].path;
  JsonVariantConst member = teamDoc_["team"][memberIndex];
  if (PokemonBookDataStore::savePokemonDocument(bookPath, member)) {
    ensureSpritesForMember(member);
    statusMessage_ = teamNames_[memberIndex] + " -> " + books_[chosenBookIndex_].title;
  } else {
    LOG_ERR("PKM", "on-device assign save failed: %s", bookPath.c_str());
    statusMessage_ = std::string(tr(STR_PARTY_SAVE_FAILED));
  }
  requestUpdate();
}

void PokemonAssignActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 I18N.get(StrId::STR_PARTY_ASSIGN), nullptr);

  // While a picker is on top this brief frame just shows a status line; the Done
  // and NoTeam phases show the result message and wait for Back.
  const char* message = (phase_ == Phase::Done || phase_ == Phase::NoTeam) ? statusMessage_.c_str() : "...";
  UITheme::drawCenteredText(renderer, Rect{0, 0, pageWidth, pageHeight}, UI_12_FONT_ID, pageHeight / 2, message, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

#endif  // ENABLE_POKEMON_PARTY
