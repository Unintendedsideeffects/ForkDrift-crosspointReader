#pragma once

#include <algorithm>
#include <cstring>
#include <vector>

#include "SettingInfo.h"

struct SettingsTopicGroup {
  StrId header;
  std::vector<const char*> keys;
};

inline void groupSettingsByTopic(std::vector<SettingInfo>& settings, const std::vector<SettingsTopicGroup>& topics) {
  std::vector<SettingInfo> out;
  out.reserve(settings.size() + topics.size());
  std::vector<bool> used(settings.size(), false);

  for (const auto& topic : topics) {
    std::vector<SettingInfo> groupItems;
    for (const char* key : topic.keys) {
      for (size_t i = 0; i < settings.size(); ++i) {
        if (!used[i] && settings[i].key != nullptr && std::strcmp(settings[i].key, key) == 0) {
          groupItems.push_back(std::move(settings[i]));
          used[i] = true;
          break;
        }
      }
    }
    if (groupItems.empty()) continue;
    out.push_back(SettingInfo::SectionHeader(topic.header));
    for (auto& item : groupItems) out.push_back(std::move(item));  // cppcheck-suppress useStlAlgorithm
  }

  for (size_t i = 0; i < settings.size(); ++i) {
    if (!used[i]) out.push_back(std::move(settings[i]));  // cppcheck-suppress useStlAlgorithm
  }
  settings = std::move(out);
}

namespace settings_topics {

inline const std::vector<SettingsTopicGroup> kDisplay{
    {StrId::STR_SEC_APPEARANCE, {"uiTheme", "recentBooksView", "darkModeScope", "fadingFix"}},
    {StrId::STR_SEC_STATUS_BAR, {"globalStatusBarPosition"}},
    {StrId::STR_SEC_SLEEP,
     {"sleepScreenSplit", "sleepScreen", "sleepScreenReader", "sleepScreenHome", "sleepScreenSource",
      "sleepScreenCoverMode", "sleepScreenCoverFilter", "sleepCycleMode", "haikuClockLandscape"}},
    {StrId::STR_SEC_DISPLAY_MISC, {"refreshFrequency"}},
};

inline const std::vector<SettingsTopicGroup> kReader{
    {StrId::STR_SEC_TEXT,
     {"fontFamily", "userFontPath", "fontSize", "lineSpacing", "textAntiAliasing", "hyphenationEnabled",
      "embeddedStyle"}},
    {StrId::STR_SEC_LAYOUT,
     {"orientation", "paragraphAlignment", "screenMargin", "extraParagraphSpacing", "forceParagraphIndents"}},
    {StrId::STR_SEC_READING_AIDS, {"focusReadingEnabled", "guideReadingEnabled", "imageRendering"}},
    {StrId::STR_SEC_STATUS_BAR, {"hideBatteryPercentage"}},
};

inline const std::vector<const char*> kGeneralSystemKeys = {
    "sleepTimeoutMinutes",
    "showHiddenFiles",
    "todoOpenDirectToToday",
    "moveFinishedToReadFolder",
};

inline bool isDeferredSystemSettingKey(const char* key) {
  if (key == nullptr) return false;
  const auto matches = [key](const char* candidate) { return std::strcmp(key, candidate) == 0; };
  if (std::any_of(kGeneralSystemKeys.begin(), kGeneralSystemKeys.end(), matches)) return true;
  static const char* kAdvancedKeys[] = {"ankiConnectUrl", "ankiConnectDeck", "backgroundServerMode", "developerMode",
                                        "deviceName"};
  return std::any_of(std::begin(kAdvancedKeys), std::end(kAdvancedKeys), matches);
}

}  // namespace settings_topics
