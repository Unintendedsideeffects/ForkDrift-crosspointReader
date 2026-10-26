#include "KOReaderSettingsActivity.h"

#include <GfxRenderer.h>
#include <HttpHeader.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <vector>

#include "KOReaderAuthActivity.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/ListPickerActivity.h"
#include "features/status_overlay/Layout.h"
#include "fontIds.h"

namespace {
constexpr int IDX_PROFILE = 0;
constexpr int IDX_PROFILE_NAME = 1;
constexpr int IDX_USER = 2;
constexpr int IDX_PASS = 3;
constexpr int IDX_URL = 4;
constexpr int IDX_HEADER1 = 5;
constexpr int IDX_HEADER2 = 6;
constexpr int IDX_MATCH = 7;
constexpr int IDX_AUTH = 8;
constexpr int MENU_ITEMS = 9;

std::string profileLabel(const size_t index) {
  const auto& profile = KOREADER_STORE.getProfiles()[index];
  if (!profile.name.empty()) {
    return profile.name;
  }
  return std::to_string(index + 1);
}
}  // namespace

void KOReaderSettingsActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;
  requestUpdate();
}

void KOReaderSettingsActivity::onExit() { Activity::onExit(); }

void KOReaderSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    requestUpdate();
  }
}

void KOReaderSettingsActivity::handleSelection() {
  const auto startKeyboard = [this](const char* title, const std::string& initial, size_t maxLength, InputType type,
                                    auto onText) {
    auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, title, initial, maxLength, type);
    if (!keyboard) {
      LOG_ERR("KRS", "OOM: KeyboardEntryActivity");
      return;
    }
    startActivityForResult(std::move(keyboard), [onText](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        onText(kb.text);
        KOREADER_STORE.saveToFile();
      }
    });
  };

  switch (static_cast<int>(selectedIndex)) {
    case IDX_PROFILE: {
      std::vector<std::string> labels;
      labels.reserve(KOReaderCredentialStore::PROFILE_COUNT);
      for (size_t i = 0; i < KOReaderCredentialStore::PROFILE_COUNT; ++i) {
        labels.push_back(profileLabel(i));
      }
      auto picker =
          makeUniqueNoThrow<ListPickerActivity>(renderer, mappedInput, StrId::STR_SYNC_PROFILE, std::move(labels),
                                                static_cast<int>(KOREADER_STORE.getActiveProfileIndex()));
      if (!picker) {
        LOG_ERR("KRS", "OOM: ListPickerActivity");
        return;
      }
      startActivityForResult(std::move(picker), [](const ActivityResult& result) {
        if (result.isCancelled) {
          return;
        }
        const auto& picked = std::get<ListPickerResult>(result.data);
        if (picked.selectedIndex >= 0) {
          KOREADER_STORE.setActiveProfileIndex(static_cast<size_t>(picked.selectedIndex));
          KOREADER_STORE.saveToFile();
        }
      });
      return;
    }
    case IDX_PROFILE_NAME:
      startKeyboard(tr(STR_SYNC_PROFILE_NAME), KOREADER_STORE.getProfileName(), 32, InputType::Text,
                    [](const std::string& text) { KOREADER_STORE.setProfileName(text); });
      return;
    case IDX_USER:
      startKeyboard(tr(STR_KOREADER_USERNAME), KOREADER_STORE.getUsername(), 64, InputType::Text,
                    [](const std::string& text) { KOREADER_STORE.setCredentials(text, KOREADER_STORE.getPassword()); });
      return;
    case IDX_PASS:
      startKeyboard(tr(STR_KOREADER_PASSWORD), KOREADER_STORE.getPassword(), 64, InputType::Password,
                    [](const std::string& text) { KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), text); });
      return;
    case IDX_URL: {
      const std::string currentUrl = KOREADER_STORE.getServerUrl();
      const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
      startKeyboard(tr(STR_SYNC_SERVER_URL), prefillUrl, 128, InputType::Url, [](const std::string& text) {
        KOREADER_STORE.setServerUrl((text == "https://" || text == "http://") ? "" : text);
      });
      return;
    }
    case IDX_HEADER1:
    case IDX_HEADER2: {
      const size_t slot = static_cast<size_t>(selectedIndex - IDX_HEADER1);
      const StrId titleId = slot == 0 ? StrId::STR_CUSTOM_HEADER_1 : StrId::STR_CUSTOM_HEADER_2;
      startKeyboard(I18N.get(titleId), formatHttpHeaderLine(KOREADER_STORE.getCustomHeaders()[slot]), 160,
                    InputType::Text, [slot](const std::string& text) {
                      HttpHeader parsed;
                      if (parseHttpHeaderLine(text, parsed)) {
                        KOREADER_STORE.setCustomHeader(slot, parsed.name, parsed.value);
                      }
                    });
      return;
    }
    case IDX_MATCH: {
      const auto current = KOREADER_STORE.getMatchMethod();
      const auto newMethod =
          (current == DocumentMatchMethod::FILENAME) ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
      KOREADER_STORE.setMatchMethod(newMethod);
      KOREADER_STORE.saveToFile();
      requestUpdate();
      return;
    }
    case IDX_AUTH:
      if (!KOREADER_STORE.hasCredentials()) {
        return;
      }
      startActivityForResult(std::make_unique<KOReaderAuthActivity>(renderer, mappedInput),
                             [](const ActivityResult&) {});
      return;
    default:
      return;
  }
}

void KOReaderSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  const int top = features::status_overlay::topInset();
  renderer.drawCenteredText(UI_12_FONT_ID, 15 + top, tr(STR_KOREADER_SYNC), true, EpdFontFamily::BOLD);

  renderer.fillRect(0, 60 + top + selectedIndex * 30 - 2, pageWidth - 1, 30);

  const StrId names[] = {StrId::STR_SYNC_PROFILE,      StrId::STR_SYNC_PROFILE_NAME, StrId::STR_KOREADER_USERNAME,
                         StrId::STR_KOREADER_PASSWORD, StrId::STR_SYNC_SERVER_URL,   StrId::STR_CUSTOM_HEADER_1,
                         StrId::STR_CUSTOM_HEADER_2,   StrId::STR_DOCUMENT_MATCHING, StrId::STR_KOREADER_AUTH};

  for (int i = 0; i < MENU_ITEMS; i++) {
    const int settingY = 60 + top + i * 30;
    const bool isSelected = (i == selectedIndex);

    renderer.drawText(UI_10_FONT_ID, 20, settingY, I18N.get(names[i]), !isSelected);

    std::string status;
    switch (i) {
      case IDX_PROFILE:
        status = profileLabel(KOREADER_STORE.getActiveProfileIndex());
        break;
      case IDX_PROFILE_NAME:
        status = KOREADER_STORE.getProfileName().empty() ? tr(STR_NOT_SET) : KOREADER_STORE.getProfileName();
        break;
      case IDX_USER:
        status = KOREADER_STORE.getUsername().empty() ? tr(STR_NOT_SET) : "[Set]";
        break;
      case IDX_PASS:
        status = KOREADER_STORE.getPassword().empty() ? tr(STR_NOT_SET) : "[Set]";
        break;
      case IDX_URL:
        status = KOREADER_STORE.getServerUrl().empty() ? "[Default]" : "[Custom]";
        break;
      case IDX_HEADER1:
        status = formatHttpHeaderLine(KOREADER_STORE.getCustomHeaders()[0], true);
        break;
      case IDX_HEADER2:
        status = formatHttpHeaderLine(KOREADER_STORE.getCustomHeaders()[1], true);
        break;
      case IDX_MATCH:
        status = KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME ? tr(STR_FILENAME) : tr(STR_BINARY);
        break;
      case IDX_AUTH:
        status = KOREADER_STORE.hasCredentials() ? "" : tr(STR_SET_CREDENTIALS_FIRST);
        break;
      default:
        break;
    }

    const auto width = renderer.getTextWidth(UI_10_FONT_ID, status.c_str());
    renderer.drawText(UI_10_FONT_ID, pageWidth - 20 - width, settingY, status.c_str(), !isSelected);
  }

  const auto labels = mappedInput.mapLabels("« Back", "Select", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
