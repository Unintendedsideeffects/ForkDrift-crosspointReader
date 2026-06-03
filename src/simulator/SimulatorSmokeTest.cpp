#ifdef SIMULATOR

#include "SimulatorSmokeTest.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"

extern ActivityManager activityManager;
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace {

enum class SmokeStep : uint8_t {
  Start,
  Home,
  FileBrowser,
  RecentBooks,
  Settings,
  SettingsNavRun,
  SettingsDone,
  Sleep,
  Reader,
  ReaderInput,
  HomeNav,
  HomeNavRun,
  Done,
};

class SimulatorSmokeTest {
 public:
  void tick() {
    if (!enabled()) return;
    try {
      tickImpl();
    } catch (const std::exception& e) {
      fail("Unhandled exception: %s", e.what());
    } catch (...) {
      fail("Unhandled non-standard exception");
    }
  }

 private:
  enum class ScriptActionType : uint8_t { Press, Release, Render };

  struct ScriptAction {
    ScriptActionType type;
    MappedInputManager::Button button;
    const char* label;
    int settleFrames;
  };

  SmokeStep step = SmokeStep::Start;
  int settleFrames = 0;
  const char* activeStepName = nullptr;
  std::vector<ScriptAction> inputScript;
  size_t scriptIndex = 0;
  // The step a Render action re-queues to (the script that owns it), and the
  // step to advance to once the running script is exhausted. Lets ReaderInput
  // and CarouselNav share one script runner.
  SmokeStep scriptStep = SmokeStep::ReaderInput;
  SmokeStep scriptDoneStep = SmokeStep::Done;

  static bool enabled() { return std::getenv("FORKDRIFT_SIMULATOR_SMOKE_TEST") != nullptr; }

  static int pageTurnCount() {
    const char* raw = std::getenv("FORKDRIFT_SIMULATOR_SMOKE_PAGE_TURNS");
    if (raw == nullptr || raw[0] == '\0') return 2;
    return std::max(0, std::atoi(raw));
  }

  static void applyRequestedTheme() {
    const char* raw = std::getenv("FORKDRIFT_SIMULATOR_SMOKE_THEME");
    if (raw == nullptr || raw[0] == '\0') return;
    const int theme = std::atoi(raw);
    constexpr int kThemeCount = static_cast<int>(CrossPointSettings::TERMINAL) + 1;
    if (theme < 0 || theme >= kThemeCount) {
      fail("Invalid smoke test theme index: %d", theme);
    }
    SETTINGS.uiTheme = static_cast<uint8_t>(theme);
    UITheme::getInstance().reload();
    LOG_INF("SMOKE", "Using theme index %d", theme);
  }

  [[noreturn]] static void fail(const char* message) {
    LOG_ERR("SMOKE", "%s", message);
    std::_Exit(2);
  }

  template <typename... Args>
  [[noreturn]] static void fail(const char* format, Args... args) {
    logPrintf("ERR", "SMOKE", format, args...);
    logPrintf("ERR", "SMOKE", "\n");
    std::_Exit(2);
  }

  static void renderCurrentStep(const char* name) {
    LOG_INF("SMOKE", "Rendering %s", name);
    activityManager.requestUpdateAndWait();
  }

  void queueStep(const char* name, SmokeStep nextStep, int framesToSettle = 3) {
    activeStepName = name;
    settleFrames = framesToSettle;
    step = nextStep;
  }

  void tickImpl() {
    mappedInputManager.simulatorClearInputFrame();

    if (settleFrames > 0) {
      --settleFrames;
      if (settleFrames == 0 && activeStepName != nullptr) {
        renderCurrentStep(activeStepName);
        activeStepName = nullptr;
      }
      return;
    }

    switch (step) {
      case SmokeStep::Start:
        LOG_INF("SMOKE", "Starting ForkDrift simulator smoke test");
        applyRequestedTheme();
        activityManager.goHome();
        queueStep("Home", SmokeStep::Home);
        break;

      case SmokeStep::Home:
        activityManager.goToFileBrowser("/books");
        queueStep("File Browser", SmokeStep::FileBrowser);
        break;

      case SmokeStep::FileBrowser:
        activityManager.goToRecentBooks();
        queueStep("Recent Books", SmokeStep::RecentBooks);
        break;

      case SmokeStep::RecentBooks:
        activityManager.goToSettings();
        queueStep("Settings", SmokeStep::Settings);
        break;

      case SmokeStep::Settings:
        // Drive the Settings menu: cycle every category tab and scroll each
        // (now topic-grouped, section-header) list to exercise header skipping.
        buildSettingsInputScript();
        scriptStep = SmokeStep::SettingsNavRun;
        scriptDoneStep = SmokeStep::SettingsDone;
        step = SmokeStep::SettingsNavRun;
        break;

      case SmokeStep::SettingsNavRun:
        runInputScript();
        break;

      case SmokeStep::SettingsDone:
        activityManager.goToSleep();
        queueStep("Sleep", SmokeStep::Sleep);
        break;

      case SmokeStep::Sleep: {
        const char* bookPath = std::getenv("FORKDRIFT_SIMULATOR_SMOKE_BOOK");
        if (bookPath == nullptr || bookPath[0] == '\0') {
          LOG_INF("SMOKE", "Skipping Reader step; FORKDRIFT_SIMULATOR_SMOKE_BOOK is not set");
          step = SmokeStep::Done;
          break;
        }
        if (!Storage.exists(bookPath)) {
          fail("Smoke test book is missing: %s", bookPath);
        }
        activityManager.goToReader(bookPath, true);
        queueStep("Reader", SmokeStep::Reader, 8);
        break;
      }

      case SmokeStep::Reader:
        buildReaderInputScript();
        scriptStep = SmokeStep::ReaderInput;
        // After the reader closes we are back Home with a recent book present.
        // Drive the Home menu for whatever theme is active to exercise its
        // selection navigation + activation path (all themes now share one
        // menuModel, so this covers carousel, grid, and classic-list nav).
        scriptDoneStep = SmokeStep::HomeNav;
        step = SmokeStep::ReaderInput;
        break;

      case SmokeStep::ReaderInput:
        runInputScript();
        break;

      case SmokeStep::HomeNav:
        buildHomeNavInputScript();
        scriptStep = SmokeStep::HomeNavRun;
        scriptDoneStep = SmokeStep::Done;
        step = SmokeStep::HomeNavRun;
        break;

      case SmokeStep::HomeNavRun:
        runInputScript();
        break;

      case SmokeStep::Done:
        LOG_INF("SMOKE", "Simulator smoke test passed");
        std::_Exit(0);
    }
  }

  static ScriptAction press(MappedInputManager::Button button) { return {ScriptActionType::Press, button, nullptr, 0}; }
  static ScriptAction release(MappedInputManager::Button button) {
    return {ScriptActionType::Release, button, nullptr, 0};
  }
  static ScriptAction render(const char* label, int frames = 3) {
    return {ScriptActionType::Render, MappedInputManager::Button::Back, label, frames};
  }

  void addTap(MappedInputManager::Button button) {
    inputScript.push_back(press(button));
    inputScript.push_back(release(button));
  }

  void buildReaderInputScript() {
    inputScript.clear();
    scriptIndex = 0;
    const int turns = pageTurnCount();
    for (int i = 0; i < turns; i++) {
      addTap(MappedInputManager::Button::PageForward);
      inputScript.push_back(render("Reader after page forward", 4));
    }
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Home after closing reader", 4));
    LOG_INF("SMOKE", "Running reader input script with %d page turn(s)", turns);
  }

  // Drives the Home menu to exercise the selection navigation + activation path
  // for whatever theme is active. All three nav modes (Lyra carousel, ForkDrift
  // grid, classic list) now index the single HomeActivity::menuModel, so the
  // same Down/Right/Up/Confirm sequence is safe and meaningful for each: it
  // moves into the menu, cycles past the end to wrap, and finally activates an
  // entry. Because the count cycled matches the activatable entries by
  // construction, this guards the unification against regression.
  void buildHomeNavInputScript() {
    inputScript.clear();
    scriptIndex = 0;
    inputScript.push_back(render("Home menu nav", 4));
    addTap(MappedInputManager::Button::Down);  // into menu row / move selection
    inputScript.push_back(render("Home menu enter", 3));
    // Cycle further than any plausible menu length to wrap fully and revisit
    // every reachable item without depending on the exact feature-gated count.
    for (int i = 0; i < 10; i++) {
      addTap(MappedInputManager::Button::Right);
      inputScript.push_back(render("Home menu cycle", 1));
    }
    for (int i = 0; i < 10; i++) {
      addTap(MappedInputManager::Button::Down);
      inputScript.push_back(render("Home menu down", 1));
    }
    addTap(MappedInputManager::Button::Up);
    inputScript.push_back(render("Home menu up", 3));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Home menu activated", 5));
    LOG_INF("SMOKE", "Running home menu navigation script");
  }

  // Cycles all four Settings category tabs and scrolls each list. Settings nav
  // skips SECTION_HEADER rows, so this exercises the topic-grouped layout
  // (Appearance/Sleep/Text/Layout/General/Connectivity/... headers). A Down tap
  // scrolls items; Confirm at the tab row (index 0) advances the category, and
  // Back from a scrolled position only returns to the tab row (never exits), so
  // the sequence can't fall out of Settings mid-script.
  void buildSettingsInputScript() {
    inputScript.clear();
    scriptIndex = 0;
    inputScript.push_back(render("Settings", 4));
    for (int cat = 0; cat < 4; cat++) {
      for (int i = 0; i < 8; i++) {
        addTap(MappedInputManager::Button::Down);
        inputScript.push_back(render("Settings scroll", 1));
      }
      addTap(MappedInputManager::Button::Back);     // scrolled position -> tab row
      addTap(MappedInputManager::Button::Confirm);  // tab row -> next category
      inputScript.push_back(render("Settings category", 3));
    }
    LOG_INF("SMOKE", "Running settings navigation script");
  }

  void runInputScript() {
    if (scriptIndex >= inputScript.size()) {
      step = scriptDoneStep;
      return;
    }
    const auto& action = inputScript[scriptIndex++];
    switch (action.type) {
      case ScriptActionType::Press:
        mappedInputManager.simulatorInjectPress(action.button);
        break;
      case ScriptActionType::Release:
        mappedInputManager.simulatorInjectRelease(action.button);
        break;
      case ScriptActionType::Render:
        queueStep(action.label, scriptStep, action.settleFrames);
        break;
    }
  }
};

SimulatorSmokeTest smokeTest;

}  // namespace

void runSimulatorSmokeTestTick() { smokeTest.tick(); }

#endif
