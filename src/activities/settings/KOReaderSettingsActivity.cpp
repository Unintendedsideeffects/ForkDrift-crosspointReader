#include "KOReaderSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <cstring>

#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/StatsUploadActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/InputReleaseGuard.h"

namespace fui = freeink::ui;

namespace {
// Display order. Headings attach to the first row of their group.
enum Row : uint8_t {
  ROW_USERNAME,
  ROW_PASSWORD,
  ROW_SERVER_URL,
  ROW_SIGN_UP,
  ROW_AUTHENTICATE,
  ROW_SYNC_ALL,
  ROW_INCLUDE_STATS,
  ROW_INCLUDE_CLIPPINGS,
  ROW_SYNC_BEHAVIOR,
  ROW_DOCUMENT_MATCHING,
  ROW_SEND_METADATA,
  ROW_COUNT
};
constexpr StrId ROW_LABELS[ROW_COUNT] = {
    StrId::STR_USERNAME,      StrId::STR_PASSWORD,          StrId::STR_SYNC_SERVER_URL, StrId::STR_SIGN_UP,
    StrId::STR_AUTHENTICATE,  StrId::STR_SYNC_ALL_BOOKS,    StrId::STR_READING_STATS,   StrId::STR_CLIPPINGS,
    StrId::STR_SYNC_BEHAVIOR, StrId::STR_DOCUMENT_MATCHING, StrId::STR_SEND_METADATA};
constexpr fui::ActionId ACTION_ROW = 1;

bool serverLacksExtensions() { return KOREADER_STORE.getServerSupport() == SyncServerSupport::UNSUPPORTED; }

bool isExtensionRow(const int row) { return row == ROW_INCLUDE_STATS || row == ROW_INCLUDE_CLIPPINGS; }
}  // namespace

KOReaderSettingsActivity::KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("KOReaderSettings", renderer, mappedInput),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void KOReaderSettingsActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<KOReaderSettingsActivity*>(user);
  if (event.value < 0 || event.value >= ROW_COUNT) return;
  {
    // listNav and the viewport are render-owned; the render task may be mid-pass on S3.
    RenderLock lock(*self);
    self->selectedIndex = static_cast<size_t>(event.value);
    self->listNav.selected = event.value;
    self->topIndex = self->listNav.top;
  }
  // Activation opens a keyboard/sub-activity or repaints a new value; a
  // lingering flash would gray an unrelated row.
  self->app.clearTapFlash();
  self->handleSelection();
}

void KOReaderSettingsActivity::onEnter() {
  Activity::onEnter();

  ignoreInitialConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  selectedIndex = 0;
  listNav.selected = 0;
  listNav.top = 0;
  uiReady = false;
  topIndex = 0;
  applySharedUiTheme(app, uiTarget);
  app.on(ACTION_ROW, &KOReaderSettingsActivity::onRowEvent, this);
  app.setScreen(&KOReaderSettingsActivity::listScreen, this);
  KOREADER_STORE.ensureLoaded();
  requestUpdate();
}

void KOReaderSettingsActivity::onExit() { Activity::onExit(); }

void KOReaderSettingsActivity::moveSelection(const size_t next) {
  {
    RenderLock lock(*this);
    selectedIndex = next;
    listNav.selected = static_cast<int>(next);
    listNav.top = topIndex;
    listNav.follow(ROW_COUNT);
    topIndex = listNav.top;
  }
  requestUpdate();
}

void KOReaderSettingsActivity::loop() {
  if (InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Confirm,
                                               ignoreInitialConfirmRelease)) {
    return;
  }

  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer)) {
    finishAfterBackPress();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finishAfterBackPress();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  // Touch goes through the FreeInkApp: render() registered the row hit rects;
  // route the snapshot and let onRowEvent dispatch.
  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;  // dispatched to onRowEvent
    }
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    {
      RenderLock lock(*this);
      listNav.top = topIndex;
      const int page = listNav.pageRowsFor(ROW_COUNT);
      listNav.scrollBy(swipe == MappedInputManager::SwipeDir::Up ? page : -page, ROW_COUNT);
      topIndex = listNav.top;
    }
    requestUpdate();
    return;
  }

  buttonNavigator.onNext([this] { moveSelection((selectedIndex + 1) % ROW_COUNT); });
  buttonNavigator.onPrevious([this] { moveSelection((selectedIndex + ROW_COUNT - 1) % ROW_COUNT); });
}

void KOReaderSettingsActivity::handleSelection() {
  // Stats and clippings need the CrossPoint extension API.
  if (isExtensionRow(static_cast<int>(selectedIndex)) && serverLacksExtensions()) return;

  switch (selectedIndex) {
    case ROW_USERNAME:
      startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_USERNAME),
                                                                     KOREADER_STORE.getUsername(), 64, InputType::Text),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) {
                                 const auto& kb = std::get<KeyboardResult>(result.data);
                                 KOREADER_STORE.setCredentials(kb.text, KOREADER_STORE.getPassword());
                                 KOREADER_STORE.saveToFile();
                               }
                             });
      break;
    case ROW_PASSWORD:
      startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_PASSWORD),
                                                                     KOREADER_STORE.getPassword(), 64, InputType::Text),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) {
                                 const auto& kb = std::get<KeyboardResult>(result.data);
                                 KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), kb.text);
                                 KOREADER_STORE.saveToFile();
                               }
                             });
      break;
    case ROW_SERVER_URL: {
      // Prefill with https:// if empty to save typing
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
      break;
    }
    case ROW_SIGN_UP:
      // Create a new account on the sync server with the entered credentials
      if (!KOREADER_STORE.hasCredentials()) return;
      silentRestartToNetwork(NetworkBootTarget::KOREADER_AUTH, 1);
      break;
    case ROW_AUTHENTICATE:
      // Can't authenticate without credentials
      if (!KOREADER_STORE.hasCredentials()) return;
      silentRestartToNetwork(NetworkBootTarget::KOREADER_AUTH);
      break;
    case ROW_INCLUDE_STATS:
    case ROW_INCLUDE_CLIPPINGS:
      // Flipping the shown value records an explicit choice over the server default.
      if (selectedIndex == ROW_INCLUDE_STATS)
        KOREADER_STORE.setSyncStats(!KOREADER_STORE.getSyncStats());
      else
        KOREADER_STORE.setSyncClippings(!KOREADER_STORE.getSyncClippings());
      KOREADER_STORE.saveToFile();
      requestUpdate();
      break;
    case ROW_SYNC_ALL: {
      // Progress for every Library book, plus stats and clippings where enabled.
      if (!KOREADER_STORE.hasCredentials()) return;
      auto sync = makeUniqueNoThrow<StatsUploadActivity>(renderer, mappedInput);
      if (!sync) {
        LOG_ERR("StatsSync", "Cannot allocate Sync All Books activity");
        return;
      }
      activityManager.replaceActivity(std::move(sync));
      break;
    }
    case ROW_SYNC_BEHAVIOR: {
      const auto current = KOREADER_STORE.getSyncBehavior();
      KOREADER_STORE.setSyncBehavior(current == KOReaderSyncBehavior::ASK_EVERY_TIME
                                         ? KOReaderSyncBehavior::SMART
                                         : KOReaderSyncBehavior::ASK_EVERY_TIME);
      KOREADER_STORE.saveToFile();
      requestUpdate();
      break;
    }
    case ROW_DOCUMENT_MATCHING: {
      const auto current = KOREADER_STORE.getMatchMethod();
      KOREADER_STORE.setMatchMethod(current == DocumentMatchMethod::FILENAME ? DocumentMatchMethod::BINARY
                                                                             : DocumentMatchMethod::FILENAME);
      KOREADER_STORE.saveToFile();
      requestUpdate();
      break;
    }
    case ROW_SEND_METADATA:
      KOREADER_STORE.setSendMetadata(!KOREADER_STORE.getSendMetadata());
      KOREADER_STORE.saveToFile();
      requestUpdate();
      break;
    default:
      break;
  }
}

void KOReaderSettingsActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<KOReaderSettingsActivity*>(user)->buildListScreen(screen);
}

void KOReaderSettingsActivity::provideRow(void* user, const uint16_t row, fui::ListItem& item) {
  static_cast<KOReaderSettingsActivity*>(user)->fillRow(row, item);
}

void KOReaderSettingsActivity::fillRow(const uint16_t row, fui::ListItem& item) {
  item.label = I18N.get(ROW_LABELS[row]);
  item.actionValue = static_cast<int16_t>(row);
  rowValue.clear();
  const bool needsCredentials = !KOREADER_STORE.hasCredentials();
  const bool noExtensions = serverLacksExtensions();
  switch (row) {
    case ROW_USERNAME: {
      const auto& username = KOREADER_STORE.getUsername();
      rowValue = username.empty() ? tr(STR_NOT_SET) : username;
      break;
    }
    case ROW_PASSWORD:
      rowValue = KOREADER_STORE.getPassword().empty() ? tr(STR_NOT_SET) : "******";
      break;
    case ROW_SERVER_URL:
      rowValue = KOREADER_STORE.getServerUrl();
      if (rowValue.empty()) {
        // Show which server the default actually is, scheme stripped for space
        std::string defaultUrl = KOREADER_STORE.getBaseUrl();
        const auto schemeEnd = defaultUrl.find("://");
        if (schemeEnd != std::string::npos) defaultUrl.erase(0, schemeEnd + 3);
        rowValue = std::string(tr(STR_DEFAULT_VALUE)) + ": " + defaultUrl;
      }
      break;
    case ROW_SIGN_UP:
    case ROW_AUTHENTICATE:
    case ROW_SYNC_ALL:
      if (needsCredentials) rowValue = std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
      break;
    case ROW_INCLUDE_STATS:
    case ROW_INCLUDE_CLIPPINGS:
      item.sectionHeading = row == ROW_INCLUDE_STATS ? tr(STR_WHAT_TO_SYNC) : nullptr;
      if (noExtensions) {
        rowValue = tr(STR_REQUIRES_CROSSPOINT_SYNC);
        item.enabled = false;
      } else {
        item.toggle = true;
        item.toggleChecked =
            row == ROW_INCLUDE_STATS ? KOREADER_STORE.getSyncStats() : KOREADER_STORE.getSyncClippings();
      }
      break;
    case ROW_SYNC_BEHAVIOR:
      item.sectionHeading = tr(STR_SYNC_OPTIONS);
      rowValue =
          KOREADER_STORE.getSyncBehavior() == KOReaderSyncBehavior::SMART ? tr(STR_SMART_SYNC) : tr(STR_ASK_EVERY_TIME);
      break;
    case ROW_DOCUMENT_MATCHING:
      rowValue = KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME ? tr(STR_FILENAME) : tr(STR_BINARY);
      break;
    case ROW_SEND_METADATA:
      item.toggle = true;
      item.toggleChecked = KOREADER_STORE.getSendMetadata();
      break;
    default:
      break;
  }
  if (!rowValue.empty()) item.value = rowValue.c_str();
}

void KOReaderSettingsActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  setUiContentMargin(screen, renderer,
                     fui::Insets{static_cast<int16_t>(TouchHeaderBackButton::contentTop(renderer, mappedInput)), 0,
                                 static_cast<int16_t>(UITheme::getButtonHintsReserve(renderer)), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.rowProvider = &KOReaderSettingsActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = ROW_COUNT;
  props.selectedIndex = static_cast<int16_t>(selectedIndex);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  configureUiList(props, screen.theme(), screen.body());
  // Bold group headings, as in Library settings; Rounded Raff restyles them below.
  props.headerText = screen.theme().bodyText;
  props.headerText.bold = true;
  configureUiListSectionHeaders(props, screen.theme());
  listNav.selected = static_cast<int>(selectedIndex);
  listNav.top = topIndex;
  // Section headings make rows variable height; ListNav pages by what was drawn.
  listNav.syncToProps(screen.body(), props.rowHeight, props.rowGap, ROW_COUNT, props);
  topIndex = listNav.top;
  screen.list(props);
}

void KOReaderSettingsActivity::render(RenderLock&&) {
  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  uiReady = false;
  // A section heading can push the selection off the measured page; rebuild until stable.
  for (int pass = 0; pass < 4; ++pass) {
    renderer.clearScreen();
    if (mappedInput.hasTouchHardware()) {
      TouchHeaderBackButton::draw(renderer, uiTarget, header, tr(STR_SYNC_SERVER), false);
    } else {
      GUI.drawHeader(renderer, header, tr(STR_SYNC_SERVER));
    }
    renderUiApp(app, uiTarget);
    topIndex = listNav.top;
    if (!listNav.consumeRebuildNeeded()) break;
  }
  uiReady = true;

  const auto labels =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
