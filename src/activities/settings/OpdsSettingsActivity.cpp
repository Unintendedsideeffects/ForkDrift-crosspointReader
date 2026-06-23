#include "OpdsSettingsActivity.h"

#include <GfxRenderer.h>
#include <HTTPClient.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstring>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "core/features/FeatureModules.h"
#include "core/features/KoreaderOpdsBridge.h"
#include "fontIds.h"
#include "network/http/HttpDownloader.h"

namespace {
// Editable fields: Name, URL, Username, Password.
// Existing servers also show a Delete option (BASE_ITEMS + 1).
constexpr int BASE_ITEMS = 4;
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
  // Each field edit is saved immediately so partially configured servers
  // survive navigation and power-loss scenarios.
  if (selectedIndex == 0) {
    // Server Name
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editServer.name = kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SERVER_NAME),
                                                                   editServer.name, 63, InputType::Text),
                           handler);
  } else if (selectedIndex == 1) {
    // Server URL
    const std::string prefillUrl = editServer.url.empty() ? "https://" : editServer.url;
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editServer.url = (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_OPDS_SERVER_URL),
                                                                   prefillUrl, 127, InputType::Url),
                           handler);
  } else if (selectedIndex == 2) {
    // Username
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editServer.username = kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_USERNAME),
                                                                   editServer.username, 63, InputType::Text),
                           handler);
  } else if (selectedIndex == 3) {
    // Password
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editServer.password = kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PASSWORD),
                                                                   editServer.password, 63, InputType::Password),
                           handler);
  } else if (selectedIndex == 4 && !isNewServer) {
    runTestConnection();
  } else if (selectedIndex == 5 && !isNewServer) {
    // Delete flow is only available for existing servers.
    if (!OPDS_STORE.removeServer(static_cast<size_t>(serverIndex))) {
      LOG_ERR("OPS", "Failed to remove OPDS server at index %d", serverIndex);
      showSaveError = true;
      requestUpdate();
      return;
    }
    finish();
  } else if (!isNewServer && static_cast<int>(selectedIndex) == BASE_ITEMS + 2) {
    // Use this OPDS server's host + credentials for KOReader (KOSync) progress sync:
    // derives <origin>/api/koreader, copies username/password, sets Binary matching.
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

  const StrId fieldNames[] = {StrId::STR_SERVER_NAME, StrId::STR_OPDS_SERVER_URL, StrId::STR_USERNAME,
                              StrId::STR_PASSWORD};

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
        if (index == 0) {
          return editServer.name.empty() ? std::string(tr(STR_NOT_SET)) : editServer.name;
        } else if (index == 1) {
          return editServer.url.empty() ? std::string(tr(STR_NOT_SET)) : editServer.url;
        } else if (index == 2) {
          return editServer.username.empty() ? std::string(tr(STR_NOT_SET)) : editServer.username;
        } else if (index == 3) {
          return editServer.password.empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
        }
        return std::string("");
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (showSaveError) {
    GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
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
  const int code = HttpDownloader::probeUrl(editServer.url, editServer.username, editServer.password);

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
