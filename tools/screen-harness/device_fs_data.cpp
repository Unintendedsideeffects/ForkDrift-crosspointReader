#include "device_fs_data.h"

#include <CrossPointSettings.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>

namespace {
std::vector<HarnessTeamMember> gTeamMembers;

std::string readTextFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return "";
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool readJsonNumber(const std::string& json, const char* key, uint8_t& out) {
  const std::regex pattern(std::string("\"") + key + R"("\s*:\s*([0-9]+))");
  std::smatch match;
  if (!std::regex_search(json, match, pattern)) {
    return false;
  }
  out = static_cast<uint8_t>(std::clamp(std::stoi(match[1].str()), 0, 255));
  return true;
}

bool readJsonString(const std::string& json, const char* key, char* out, const size_t outSize) {
  const std::regex pattern(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch match;
  if (!std::regex_search(json, match, pattern) || outSize == 0) {
    return false;
  }
  std::strncpy(out, match[1].str().c_str(), outSize - 1);
  out[outSize - 1] = '\0';
  return true;
}

std::vector<RecentBook> parseRecentBooksJson(const std::string& json) {
  std::vector<RecentBook> books;
  const std::regex bookPattern(
      R"re(\{\s*"path"\s*:\s*"([^"]*)"\s*,\s*"title"\s*:\s*"([^"]*)"\s*,\s*"author"\s*:\s*"([^"]*)"\s*,\s*"coverBmpPath"\s*:\s*"([^"]*)"\s*\})re");
  auto begin = std::sregex_iterator(json.begin(), json.end(), bookPattern);
  const auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) {
    books.push_back({(*it)[1].str(), (*it)[2].str(), (*it)[3].str(), (*it)[4].str()});
  }
  return books;
}

void loadTeamMembersFromJson(const std::string& json) {
  gTeamMembers.clear();
  const std::regex memberPattern(
      R"re(\{\s*"id"\s*:\s*[0-9]+\s*,\s*"name"\s*:\s*"([^"]*)"\s*,\s*"speciesId"\s*:\s*([0-9]+))re");
  auto begin = std::sregex_iterator(json.begin(), json.end(), memberPattern);
  const auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) {
    HarnessTeamMember member;
    member.name = (*it)[1].str();
    member.speciesId = std::stoi((*it)[2].str());
    if (member.speciesId > 0) {
      gTeamMembers.push_back(std::move(member));
    }
  }
}
}  // namespace

std::filesystem::path resolveDeviceFilesystemRoot() {
  if (const char* env = std::getenv("SCREEN_HARNESS_SD_ROOT")) {
    return std::filesystem::path(env);
  }
  const std::filesystem::path candidates[] = {
      std::filesystem::absolute("../deviceFilesystem"),
      std::filesystem::absolute("deviceFilesystem"),
      std::filesystem::absolute("../../deviceFilesystem"),
  };
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate / ".crosspoint")) {
      return candidate;
    }
  }
  return {};
}

bool mountDeviceFilesystem(std::filesystem::path& mountedRoot) {
  mountedRoot = resolveDeviceFilesystemRoot();
  if (mountedRoot.empty() || !std::filesystem::exists(mountedRoot / ".crosspoint")) {
    mountedRoot.clear();
    return false;
  }
  Storage.setRoot(mountedRoot);
  std::cout << "screen-harness SD root: " << mountedRoot << '\n';

  const std::string teamJson = readTextFile(mountedRoot / ".crosspoint/pokemon/team.json");
  if (!teamJson.empty()) {
    loadTeamMembersFromJson(teamJson);
  }
  return true;
}

std::vector<RecentBook> loadRecentBooksFromDevice() {
  if (!Storage.hasRoot()) {
    return {};
  }
  const std::string json = readTextFile(Storage.root() / ".crosspoint/recent.json");
  if (json.empty()) {
    return {};
  }
  return parseRecentBooksJson(json);
}

bool applySettingsFromDevice() {
  if (!Storage.hasRoot()) {
    return false;
  }
  const std::string json = readTextFile(Storage.root() / ".crosspoint/settings.json");
  if (json.empty()) {
    return false;
  }

  readJsonNumber(json, "fontFamily", SETTINGS.fontFamily);
  readJsonNumber(json, "fontSize", SETTINGS.fontSize);
  readJsonNumber(json, "lineSpacing", SETTINGS.lineSpacing);
  readJsonNumber(json, "screenMargin", SETTINGS.screenMargin);
  readJsonNumber(json, "paragraphAlignment", SETTINGS.paragraphAlignment);
  readJsonNumber(json, "embeddedStyle", SETTINGS.embeddedStyle);
  readJsonNumber(json, "hyphenationEnabled", SETTINGS.hyphenationEnabled);
  readJsonNumber(json, "orientation", SETTINGS.orientation);
  readJsonNumber(json, "extraParagraphSpacing", SETTINGS.extraParagraphSpacing);
  readJsonNumber(json, "textAntiAliasing", SETTINGS.textAntiAliasing);
  readJsonNumber(json, "imageRendering", SETTINGS.imageRendering);
  readJsonNumber(json, "frontButtonLayout", SETTINGS.frontButtonLayout);
  readJsonNumber(json, "frontButtonBack", SETTINGS.frontButtonBack);
  readJsonNumber(json, "frontButtonConfirm", SETTINGS.frontButtonConfirm);
  readJsonNumber(json, "frontButtonLeft", SETTINGS.frontButtonLeft);
  readJsonNumber(json, "frontButtonRight", SETTINGS.frontButtonRight);
  readJsonNumber(json, "sleepScreen", SETTINGS.sleepScreen);
  readJsonNumber(json, "sleepScreenSource", SETTINGS.sleepScreenSource);
  readJsonNumber(json, "sleepScreenCoverMode", SETTINGS.sleepScreenCoverMode);
  readJsonNumber(json, "sleepScreenCoverFilter", SETTINGS.sleepScreenCoverFilter);
  readJsonNumber(json, "sleepCycleMode", SETTINGS.sleepCycleMode);
  readJsonNumber(json, "darkMode", SETTINGS.darkMode);
  readJsonNumber(json, "hideBatteryPercentage", SETTINGS.hideBatteryPercentage);
  readJsonNumber(json, "uiTheme", SETTINGS.uiTheme);
  readJsonNumber(json, "refreshFrequency", SETTINGS.refreshFrequency);
  readJsonNumber(json, "fadingFix", SETTINGS.fadingFix);
  readJsonNumber(json, "globalStatusBar", SETTINGS.globalStatusBar);
  readJsonNumber(json, "globalStatusBarPosition", SETTINGS.globalStatusBarPosition);
  readJsonNumber(json, "sleepTimeoutMinutes", SETTINGS.sleepTimeoutMinutes);
  readJsonNumber(json, "sideButtonLayout", SETTINGS.sideButtonLayout);
  readJsonNumber(json, "sideButtonLongPress", SETTINGS.sideButtonLongPress);
  readJsonNumber(json, "shortPwrBtn", SETTINGS.shortPwrBtn);
  readJsonNumber(json, "longPressButtonBehavior", SETTINGS.longPressButtonBehavior);
  readJsonNumber(json, "usbMscPromptOnConnect", SETTINGS.usbMscPromptOnConnect);
  readJsonNumber(json, "backgroundServerOnCharge", SETTINGS.backgroundServerOnCharge);
  readJsonNumber(json, "wifiAutoConnect", SETTINGS.wifiAutoConnect);
  readJsonString(json, "deviceName", SETTINGS.deviceName, sizeof(SETTINGS.deviceName));
  SETTINGS.validateAndClamp();
  return true;
}

const std::vector<HarnessTeamMember>& harnessTeamMembers() { return gTeamMembers; }

int harnessBookIndex(const std::string& bookPath) {
  const auto books = loadRecentBooksFromDevice();
  for (size_t i = 0; i < books.size(); ++i) {
    if (books[i].path == bookPath) {
      return static_cast<int>(i);
    }
  }
  return -1;
}
