#include "OpdsSettingsActivity.h"

#include <GfxRenderer.h>
#include <HTTPClient.h>
#include <HttpHeader.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstring>
#include <vector>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/ListPickerActivity.h"
#include "components/UITheme.h"
#include "core/features/FeatureModules.h"
#include "core/features/KoreaderOpdsBridge.h"
#include "fontIds.h"
#include "network/http/HttpDownloader.h"
#include "util/OpdsDownloadFolder.h"

namespace {
constexpr int IDX_NAME = 0;
constexpr int IDX_URL = 1;
constexpr int IDX_USER = 2;
constexpr int IDX_PASS = 3;
constexpr int IDX_HEADER1 = 4;
constexpr int IDX_HEADER2 = 5;
constexpr int IDX_FOLDER = 6;
constexpr int BASE_ITEMS = 7;
}  // namespace

int OpdsSettingsActivity::getMenuItemCount() const {
  if (isNewServer) {
    return BASE_ITEMS;
  }
  // Existing server: +1 for Test Connection, +1 for Delete, +1 more for "Use for KOReader Sync" when that feature is
  // compiled in.
  const bool koSync = core::FeatureModules::hasCapability(core::Capability::KoreaderSync);
  return BASE_ITEMS + 2 + (koSync ? 1 : 0);
}

void OpdsSettingsActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;
  isNewServer = (serverIndex < 0);
  showSaveError = false;
  showSyncConfirmed = false;
  showInvalidFolder = false;

  if (!isNewServer) {
    // Edit flow: copy the selected server into local editable state.
    // Changes are persisted field-by-field through saveServer().
    const auto* server = OPDS_STORE.getServer(static_cast<size_t>(serverIndex));
    if (server) {
      editServer = *server;
    } else {
      // Server was deleted between navigation and entering this screen — treat as new
      isNewServer = true;
      serverIndex = -1;
    }
  }

  requestUpdate();
}

void OpdsSettingsActivity::onExit() { Activity::onExit(); }

void OpdsSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int menuItems = getMenuItemCount();
  buttonNavigator.onNext([this, menuItems] {
    selectedIndex = (selectedIndex + 1) % menuItems;
    showSyncConfirmed = false;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuItems] {
    selectedIndex = (selectedIndex + menuItems - 1) % menuItems;
    showSyncConfirmed = false;
    requestUpdate();
  });
}

bool OpdsSettingsActivity::saveServer() {
  bool success = false;

  if (isNewServer) {
    // Create flow: first save inserts a new server record into the multi-server store.
    success = OPDS_STORE.addServer(editServer);
    if (success) {
      // After the first successful save, promote to an existing server so
      // subsequent field edits update in-place rather than creating duplicates.
      isNewServer = false;
      serverIndex = static_cast<int>(OPDS_STORE.getCount()) - 1;
    } else {
      LOG_ERR("OPS", "Failed to add OPDS server");
    }
  } else {
    // Edit flow: update the same server entry in-place.
    success = OPDS_STORE.updateServer(static_cast<size_t>(serverIndex), editServer);
    if (!success) {
      LOG_ERR("OPS", "Failed to update OPDS server at index %d", serverIndex);
    }
  }

  showSaveError = !success;
  if (showSaveError) {
    requestUpdate();
  }

  return success;
}

void OpdsSettingsActivity::handleSelection() {
  showSyncConfirmed = false;
  showInvalidFolder = false;
  const auto startKeyboard = [this](const char* title, const std::string& initial, size_t maxLength, InputType type,
                                    auto onText) {
    auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, title, initial, maxLength, type);
    if (!keyboard) {
      LOG_ERR("OPS", "OOM: KeyboardEntryActivity");
      return;
    }
    startActivityForResult(std::move(keyboard), [this, onText](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        onText(kb.text);
        saveServer();
        requestUpdate();
      }
    });
  };

  switch (static_cast<int>(selectedIndex)) {
    case IDX_NAME:
      startKeyboard(tr(STR_SERVER_NAME), editServer.name, 63, InputType::Text,
                    [this](const std::string& text) { editServer.name = text; });
      return;
    case IDX_URL: {
      const std::string prefillUrl = editServer.url.empty() ? "https://" : editServer.url;
      startKeyboard(tr(STR_OPDS_SERVER_URL), prefillUrl, 127, InputType::Url, [this](const std::string& text) {
        editServer.url = (text == "https://" || text == "http://") ? "" : text;
      });
      return;
    }
    case IDX_USER:
      startKeyboard(tr(STR_USERNAME), editServer.username, 63, InputType::Text,
                    [this](const std::string& text) { editServer.username = text; });
      return;
    case IDX_PASS:
      startKeyboard(tr(STR_PASSWORD), editServer.password, 63, InputType::Password,
                    [this](const std::string& text) { editServer.password = text; });
      return;
    case IDX_HEADER1:
    case IDX_HEADER2: {
      const size_t slot = static_cast<size_t>(selectedIndex - IDX_HEADER1);
      const StrId titleId = slot == 0 ? StrId::STR_CUSTOM_HEADER_1 : StrId::STR_CUSTOM_HEADER_2;
      startKeyboard(I18N.get(titleId), formatHttpHeaderLine(editServer.customHeaders[slot]), 160, InputType::Text,
                    [this, slot](const std::string& text) {
                      HttpHeader parsed;
                      if (parseHttpHeaderLine(text, parsed)) {
                        editServer.customHeaders[slot] = std::move(parsed);
                      }
                    });
      return;
    }
    case IDX_FOLDER: {
      std::vector<std::string> choices;
      choices.reserve(2);
      choices.emplace_back(tr(STR_OPDS_SD_ROOT));
      choices.emplace_back(tr(STR_OPDS_CUSTOM_FOLDER));
      const int initial = (editServer.downloadFolder.empty() || editServer.downloadFolder == "/") ? 0 : 1;
      auto picker = makeUniqueNoThrow<ListPickerActivity>(renderer, mappedInput, StrId::STR_OPDS_DOWNLOAD_FOLDER,
                                                          std::move(choices), initial);
      if (!picker) {
        LOG_ERR("OPS", "OOM: ListPickerActivity");
        return;
      }
      startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
        if (result.isCancelled) {
          return;
        }
        const auto& picked = std::get<ListPickerResult>(result.data);
        if (picked.selectedIndex == 0) {
          editServer.downloadFolder = "/";
          saveServer();
          requestUpdate();
          return;
        }
        if (picked.selectedIndex != 1) {
          return;
        }
        auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_OPDS_CUSTOM_FOLDER),
                                                                 editServer.downloadFolder,
                                                                 OpdsDownloadFolder::MAX_LENGTH, InputType::Text);
        if (!keyboard) {
          LOG_ERR("OPS", "OOM: KeyboardEntryActivity");
          return;
        }
        startActivityForResult(std::move(keyboard), [this](const ActivityResult& kbResult) {
          if (kbResult.isCancelled) {
            return;
          }
          const auto& kb = std::get<KeyboardResult>(kbResult.data);
          std::string folder;
          if (!OpdsDownloadFolder::normalize(kb.text, folder)) {
            showInvalidFolder = true;
            requestUpdate();
            return;
          }
          editServer.downloadFolder = std::move(folder);
          saveServer();
          requestUpdate();
        });
      });
      return;
    }
    default:
      break;
  }

  if (isNewServer) {
    return;
  }
  if (static_cast<int>(selectedIndex) == BASE_ITEMS) {
    runTestConnection();
  } else if (static_cast<int>(selectedIndex) == BASE_ITEMS + 1) {
    if (!OPDS_STORE.removeServer(static_cast<size_t>(serverIndex))) {
      LOG_ERR("OPS", "Failed to remove OPDS server at index %d", serverIndex);
      showSaveError = true;
      requestUpdate();
      return;
    }
    finish();
  } else if (static_cast<int>(selectedIndex) == BASE_ITEMS + 2) {
    core::applyOpdsServerToKoreaderSync(editServer);
    showSyncConfirmed = true;
    requestUpdate();
  }
}

void OpdsSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  // Reuse STR_OPDS_BROWSER as the "edit existing server" title.
  // New server creation uses STR_ADD_SERVER.
  const char* header = isNewServer ? tr(STR_ADD_SERVER) : tr(STR_OPDS_BROWSER);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, header);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    tr(STR_CALIBRE_URL_HINT));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + metrics.tabBarHeight;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int menuItems = getMenuItemCount();

  const StrId fieldNames[] = {StrId::STR_SERVER_NAME,
                              StrId::STR_OPDS_SERVER_URL,
                              StrId::STR_USERNAME,
                              StrId::STR_PASSWORD,
                              StrId::STR_CUSTOM_HEADER_1,
                              StrId::STR_CUSTOM_HEADER_2,
                              StrId::STR_OPDS_DOWNLOAD_FOLDER};

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, menuItems, static_cast<int>(selectedIndex),
      [this, &fieldNames](int index) {
        if (index < BASE_ITEMS) {
          return std::string(I18N.get(fieldNames[index]));
        }
        if (index == BASE_ITEMS) {
          return std::string("Test Connection");
        }
        if (index == BASE_ITEMS + 1) {
          return std::string(tr(STR_DELETE_SERVER));
        }
        return std::string(tr(STR_USE_FOR_KOREADER_SYNC));
      },
      nullptr, nullptr,
      [this](int index) {
        switch (index) {
          case IDX_NAME:
            return editServer.name.empty() ? std::string(tr(STR_NOT_SET)) : editServer.name;
          case IDX_URL:
            return editServer.url.empty() ? std::string(tr(STR_NOT_SET)) : editServer.url;
          case IDX_USER:
            return editServer.username.empty() ? std::string(tr(STR_NOT_SET)) : editServer.username;
          case IDX_PASS:
            return editServer.password.empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
          case IDX_HEADER1:
            return formatHttpHeaderLine(editServer.customHeaders[0], true);
          case IDX_HEADER2:
            return formatHttpHeaderLine(editServer.customHeaders[1], true);
          case IDX_FOLDER:
            if (editServer.downloadFolder.empty() || editServer.downloadFolder == "/") {
              return std::string(tr(STR_OPDS_SD_ROOT));
            }
            return editServer.downloadFolder;
          default:
            return std::string("");
        }
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (showSaveError) {
    GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
  } else if (showInvalidFolder) {
    GUI.drawPopup(renderer, tr(STR_OPDS_INVALID_FOLDER));
  } else if (showSyncConfirmed) {
    GUI.drawPopup(renderer, tr(STR_KOREADER_SYNC_CONFIGURED));
  }

  renderer.displayBuffer();
}

void OpdsSettingsActivity::runTestConnection() {
  if (editServer.url.empty()) {
    startActivityForResult(
        std::make_unique<FullScreenMessageActivity>(renderer, mappedInput,
                                                    "Error: URL is not set.\nPlease set a valid OPDS server URL first.",
                                                    EpdFontFamily::REGULAR, [] { activityManager.popActivity(); }),
        nullptr);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        runTestConnection();
      }
    };
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), handler);
    return;
  }

  // Draw temporary testing screen
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Reuse the header
  const char* header = isNewServer ? tr(STR_ADD_SERVER) : tr(STR_OPDS_BROWSER);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, header);

  // Draw the centered status message
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + metrics.tabBarHeight;
  renderer.drawCenteredText(UI_10_FONT_ID, contentTop + (pageHeight - contentTop) / 2 - 20, "Testing Connection...",
                            true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, contentTop + (pageHeight - contentTop) / 2 + 10,
                            "Probing URL, please wait...", true, EpdFontFamily::REGULAR);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  // Call probeUrl blocking
  const int code = HttpDownloader::probeUrl(editServer.url, editServer.username, editServer.password,
                                            editServer.customHeaders.data(), OpdsServer::MAX_CUSTOM_HEADERS);

  std::string resultMsg;
  if (code >= 200 && code < 300) {
    resultMsg =
        "Connection Successful!\n\nSuccessfully connected to the OPDS server. The server is online, and credentials "
        "are correct.";
  } else if (code == 401) {
    resultMsg =
        "Connection Failed: 401 Unauthorized\n\nAuthentication failed. Please verify:\n"
        "1. Username and Password are correct.\n"
        "2. Note: passwords copied from another device/simulator might be obfuscated with a different hardware MAC "
        "address and appear as garbage. Try re-entering the password on this device.";
  } else if (code == 404) {
    resultMsg =
        "Connection Failed: 404 Not Found\n\nThe server responded, but the page was not found.\n"
        "Please check the URL path suffix. For Calibre, this often ends in '/opds' (e.g., "
        "http://192.168.1.10:8080/opds).";
  } else if (code == HTTPC_ERROR_CONNECTION_REFUSED) {
    resultMsg =
        "Connection Failed: Connection Refused\n\nThe server actively refused the connection. Please verify:\n"
        "1. The port (e.g., :10000 or :8080) is correct.\n"
        "2. The server application is running and online.\n"
        "3. If using Tailscale (e.g. ts.net), verify that the node is active and reachable from this device's subnet.";
  } else if (code == HTTPC_ERROR_READ_TIMEOUT) {
    resultMsg =
        "Connection Failed: Connection Timeout\n\nThe connection timed out waiting for the server response.\n"
        "Please verify:\n"
        "1. The server address/port is correct.\n"
        "2. The server machine is online and accessible.\n"
        "3. The network signal strength is sufficient.";
  } else if (code < 0) {
    resultMsg = "Connection Failed: Network Error (" + std::to_string(code) +
                ")\n\nUnable to establish network connection.\n"
                "Please verify:\n"
                "1. Your Wi-Fi network connection is stable.\n"
                "2. The server URL is correct.\n"
                "3. Firewalls/routers are not blocking access to the port.";
  } else {
    resultMsg = "Connection Failed: HTTP Status " + std::to_string(code) +
                "\n\nThe server responded with an unexpected status code.\n"
                "Please verify the server configuration and check its logs.";
  }

  // Show result screen
  startActivityForResult(
      std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, resultMsg, EpdFontFamily::REGULAR,
                                                  [] { activityManager.popActivity(); }),
      nullptr);
}
