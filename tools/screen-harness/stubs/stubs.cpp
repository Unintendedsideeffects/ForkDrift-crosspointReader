#include "Arduino.h"
#include "ActivityManager.h"
#include "CrossPointSettings.h"
#include "HalGPIO.h"
#include "MappedInputManager.h"
#include "activities/RenderLock.h"
#include "SPI.h"
#include "SdCardFont.h"
#include "SdCardFontManager.h"
#include "SdCardFontRegistry.h"
#include "SdCardFontSystem.h"
#include "core/features/FeatureModules.h"
#include "util/ButtonNavigator.h"

#include <algorithm>

namespace FactoryResetUtils {
bool resetCrossPointMetadataPreservingContent() { return true; }
}

HardwareSerial Serial;
EspClass ESP;
SPIClass SPI;
HalGPIO gpio;
ActivityManager activityManager;
CrossPointSettings CrossPointSettings::instance;
SdCardFontSystem sdFontSystem;

// The screen harness registers no SD-card fonts, so these SdCardFont paths in
// GfxRenderer are never exercised. Stub them (SdCardFont.cpp is not linked).
bool SdCardFont::isOverflowGlyph(const EpdGlyph*) const { return false; }
const uint8_t* SdCardFont::getOverflowBitmap(const EpdGlyph*) const { return nullptr; }
SdCardFont* SdCardFont::fromMissCtx(void*) { return nullptr; }

RenderLock::RenderLock() : isLocked(false) {}
RenderLock::RenderLock(Activity&) : isLocked(false) {}
RenderLock::~RenderLock() {}
void RenderLock::unlock() { isLocked = false; }
bool RenderLock::peek() { return false; }

void ActivityManager::requestUpdate(bool) {}
void ActivityManager::requestUpdateAndWait() {}
void ActivityManager::pushActivity(std::unique_ptr<Activity>) {}
void ActivityManager::popActivity() {}
void ActivityManager::goHome() {}
void ActivityManager::goToRecentBooks() {}
void ActivityManager::goToReader(std::string, bool) {}

bool MappedInputManager::wasPressed(Button) { return false; }
bool MappedInputManager::wasReleased(Button) { return false; }
bool MappedInputManager::isPressed(Button) const { return false; }
void MappedInputManager::clearTransientState() {}
void MappedInputManager::injectVirtualActivation(Button) {}
bool MappedInputManager::wasAnyPressed() const { return false; }
bool MappedInputManager::wasAnyReleased() const { return false; }
unsigned long MappedInputManager::getHeldTime() const { return 0; }
MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  return {back, confirm, previous, next};
}
int MappedInputManager::getPressedFrontButton() const { return -1; }
bool MappedInputManager::mapButton(Button, bool (HalGPIO::*)(uint8_t) const) const { return false; }
void MappedInputManager::updatePowerTapState() {}
bool MappedInputManager::consumePowerConfirm() { return false; }
bool MappedInputManager::consumePowerBack() { return false; }

MappedInputManager* ButtonNavigator::mappedInput = nullptr;
bool ButtonNavigator::shouldNavigateContinuously() const { return false; }
void ButtonNavigator::onNext(const Callback&) {}
void ButtonNavigator::onPrevious(const Callback&) {}
void ButtonNavigator::onPressAndContinuous(const Buttons&, const Callback&) {}
void ButtonNavigator::onNextPress(const Callback&) {}
void ButtonNavigator::onPreviousPress(const Callback&) {}
void ButtonNavigator::onPress(const Buttons&, const Callback&) {}
void ButtonNavigator::onNextRelease(const Callback&) {}
void ButtonNavigator::onPreviousRelease(const Callback&) {}
void ButtonNavigator::onRelease(const Buttons&, const Callback&) {}
void ButtonNavigator::onNextContinuous(const Callback&) {}
void ButtonNavigator::onPreviousContinuous(const Callback&) {}
void ButtonNavigator::onContinuous(const Buttons&, const Callback&) {}
int ButtonNavigator::nextIndex(int currentIndex, int totalItems) { return totalItems > 0 ? (currentIndex + 1) % totalItems : 0; }
int ButtonNavigator::previousIndex(int currentIndex, int totalItems) {
  return totalItems > 0 ? (currentIndex + totalItems - 1) % totalItems : 0;
}
int ButtonNavigator::nextPageIndex(int currentIndex, int totalItems, int itemsPerPage) {
  return std::min(totalItems - 1, currentIndex + itemsPerPage);
}
int ButtonNavigator::previousPageIndex(int currentIndex, int, int itemsPerPage) {
  return std::max(0, currentIndex - itemsPerPage);
}

bool CrossPointSettings::saveToFile() const { return true; }
void CrossPointSettings::validateAndClamp() {}
void CrossPointSettings::enforceButtonLayoutConstraints() {}
void CrossPointSettings::applyFrontButtonLayoutPreset(FRONT_BUTTON_LAYOUT) {}

SdCardFontManager::~SdCardFontManager() = default;
bool SdCardFontManager::loadFamily(const SdCardFontFamilyInfo&, GfxRenderer&, uint8_t) { return false; }
void SdCardFontManager::unloadAll(GfxRenderer&) {}
int SdCardFontManager::getFontId(const std::string&) const { return 0; }

const SdCardFontFileInfo* SdCardFontFamilyInfo::findFile(uint8_t /*size*/, uint8_t /*style*/) const { return nullptr; }
bool SdCardFontFamilyInfo::hasSize(uint8_t /*size*/) const { return false; }
std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const { return {}; }

const char* SdCardFontRegistry::findFamilyRoot(const char* /*familyName*/) { return nullptr; }
const char* SdCardFontRegistry::defaultWriteRoot() { return FONTS_DIR_HIDDEN; }
bool SdCardFontRegistry::discover() { return false; }
const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string& /*name*/) const { return nullptr; }
int SdCardFontRegistry::getFamilyIndex(const std::string& /*name*/) const { return -1; }

namespace core {
bool FeatureModules::isEnabled(const char*) { return true; }
bool FeatureModules::hasCapability(Capability capability) {
  return capability == Capability::LyraTheme || capability == Capability::DarkMode ||
         capability == Capability::GlobalStatusBar || capability == Capability::HomeMediaPicker ||
         capability == Capability::OtaUpdates || capability == Capability::UsbMassStorage ||
         capability == Capability::WebWifiSetup;
}
String FeatureModules::getBuildString() { return "screen-harness"; }
String FeatureModules::getFeatureMapJson() { return "{}"; }
bool FeatureModules::supportsSettingAction(SettingAction) { return true; }
FeatureModules::HomeCardDataResult FeatureModules::resolveHomeCardData(const std::string&, int) { return {}; }
FeatureModules::RecentBookDataResult FeatureModules::resolveRecentBookData(const std::string&) { return {}; }
bool FeatureModules::isSupportedLibraryFile(const std::string&) { return true; }
bool FeatureModules::hasKoreaderSyncCredentials() { return false; }
Activity* FeatureModules::createTodoPlannerActivity(GfxRenderer&, MappedInputManager&, std::string, std::string, void*,
                                                    void (*)(void*)) {
  return nullptr;
}
Activity* FeatureModules::createTodoFallbackActivity(GfxRenderer&, MappedInputManager&, std::string, void*,
                                                     void (*)(void*)) {
  return nullptr;
}
void FeatureModules::onFontFamilySettingChanged(uint8_t) {}
void FeatureModules::onWebSettingsApplied() {}
void FeatureModules::onUploadCompleted(const String&, const String&) {}
void FeatureModules::onWebFileChanged(const String&) {}
bool FeatureModules::tryGetDocumentCoverPath(const String&, std::string&) { return false; }
std::string FeatureModules::getKoreaderUsername() { return ""; }
std::string FeatureModules::getKoreaderPassword() { return ""; }
std::string FeatureModules::getKoreaderServerUrl() { return ""; }
uint8_t FeatureModules::getKoreaderMatchMethod() { return 0; }
void FeatureModules::setKoreaderUsername(const std::string&, bool) {}
void FeatureModules::setKoreaderPassword(const std::string&, bool) {}
void FeatureModules::setKoreaderServerUrl(const std::string&, bool) {}
void FeatureModules::setKoreaderMatchMethod(uint8_t, bool) {}
void FeatureModules::saveKoreaderSettings() {}
std::vector<std::string> FeatureModules::getUserFontFamilies() { return {}; }
uint8_t FeatureModules::getSelectedUserFontFamilyIndex() { return 0; }
void FeatureModules::setSelectedUserFontFamilyIndex(uint8_t) {}
}  // namespace core
