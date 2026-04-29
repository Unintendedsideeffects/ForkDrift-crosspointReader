#include "KOReaderSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "KOReaderAuthActivity.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "fontIds.h"

namespace {
constexpr int MENU_ITEMS = 5;
const char* menuNames[MENU_ITEMS] = {"Username", "Password", "Sync Server URL", "Document Matching", "Authenticate"};
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
  if (selectedIndex == 0) {
    // Username
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_USERNAME),
                                                                   KOREADER_STORE.getUsername(), 64, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               KOREADER_STORE.setCredentials(kb.text, KOREADER_STORE.getPassword());
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (selectedIndex == 1) {
    // Password
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_PASSWORD),
                                                KOREADER_STORE.getPassword(), 64, InputType::Password),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), kb.text);
            KOREADER_STORE.saveToFile();
          }
        });
  } else if (selectedIndex == 2) {
    // Sync Server URL - prefill with https:// if empty to save typing
    const std::string currentUrl = KOREADER_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SYNC_SERVER_URL),
                                                                   prefillUrl, 128, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               KOREADER_STORE.setServerUrl(urlToSave);
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (selectedIndex == 3) {
    // Document Matching - toggle between Filename and Binary
    const auto current = KOREADER_STORE.getMatchMethod();
    const auto newMethod =
        (current == DocumentMatchMethod::FILENAME) ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
    KOREADER_STORE.setMatchMethod(newMethod);
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (selectedIndex == 4) {
    // Authenticate
    if (!KOREADER_STORE.hasCredentials()) {
      // Can't authenticate without credentials - just show message briefly
      return;
    }
    startActivityForResult(std::make_unique<KOReaderAuthActivity>(renderer, mappedInput), [](const ActivityResult&) {});
  }
}

void KOReaderSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  // Draw header
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "KOReader Sync", true, EpdFontFamily::BOLD);

  // Draw selection highlight
  renderer.fillRect(0, 60 + selectedIndex * 30 - 2, pageWidth - 1, 30);

  // Draw menu items
  for (int i = 0; i < MENU_ITEMS; i++) {
    const int settingY = 60 + i * 30;
    const bool isSelected = (i == selectedIndex);

    renderer.drawText(UI_10_FONT_ID, 20, settingY, menuNames[i], !isSelected);

    // Draw status for each item
    const char* status = "";
    if (i == 0) {
      status = KOREADER_STORE.getUsername().empty() ? "[Not Set]" : "[Set]";
    } else if (i == 1) {
      status = KOREADER_STORE.getPassword().empty() ? "[Not Set]" : "[Set]";
    } else if (i == 2) {
      status = KOREADER_STORE.getServerUrl().empty() ? "[Default]" : "[Custom]";
    } else if (i == 3) {
      status = KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME ? "[Filename]" : "[Binary]";
    } else if (i == 4) {
      status = KOREADER_STORE.hasCredentials() ? "" : "[Set credentials first]";
    }

    const auto width = renderer.getTextWidth(UI_10_FONT_ID, status);
    renderer.drawText(UI_10_FONT_ID, pageWidth - 20 - width, settingY, status, !isSelected);
  }

  // Draw button hints
  const auto labels = mappedInput.mapLabels("« Back", "Select", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
