#include "FirmwareUpdateUtil.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <ScopedBuffer.h>
#include <Serialization.h>
#include <SpiBusMutex.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "esp_app_format.h"  // cppcheck-suppress missingInclude
#include "esp_ota_ops.h"     // cppcheck-suppress missingInclude
#include "fontIds.h"
#include "util/ButtonNavigator.h"

namespace {
constexpr char kSkippedLocalUpdatePath[] = "/.crosspoint/local-update-skip.bin";
constexpr uint8_t kSkippedLocalUpdateVersion = 1;
constexpr size_t kFingerprintSampleBytes = 4096;
constexpr size_t kVersionScanChunkBytes = 1024;
constexpr size_t kMaxFirmwareVersionLength = 96;
constexpr int kPromptItemCount = 4;

enum class LocalUpdatePromptAction : uint8_t { Install = 0, SkipForNow, SkipThisVersion, DeleteIt };

struct LocalUpdateFingerprint {
  uint32_t fileSize = 0;
  uint32_t sampleHash = 0;
};

struct LocalUpdateMetadata {
  LocalUpdateFingerprint fingerprint;
  String path;
  String candidateVersion;
};

uint32_t fnv1aUpdate(uint32_t hash, const uint8_t* data, const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

bool isDecimalDigit(const char ch) { return ch >= '0' && ch <= '9'; }

bool isHexDigit(const char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

bool isFirmwareVersionChar(const char ch) { return ch >= '!' && ch <= '~' && ch != '"' && ch != '\\'; }

void advanceMarkerMatch(const char ch, const char* marker, size_t& matchLength) {
  if (ch == marker[matchLength]) {
    ++matchLength;
    return;
  }

  matchLength = ch == marker[0] ? 1 : 0;
}

bool isNamedFirmwareFile(const char* name) {
  constexpr char prefix[] = "firmware-";
  constexpr char suffix[] = ".bin";
  constexpr size_t dateLength = 8;
  constexpr size_t minShaLength = 7;
  const size_t prefixLength = strlen(prefix);
  const size_t suffixLength = strlen(suffix);
  const size_t nameLength = name ? strlen(name) : 0;
  const size_t minLength = prefixLength + dateLength + 1 + minShaLength + suffixLength;

  if (nameLength < minLength || strncmp(name, prefix, prefixLength) != 0 ||
      strcmp(name + nameLength - suffixLength, suffix) != 0) {
    return false;
  }

  const size_t dateStart = prefixLength;
  const size_t shaStart = dateStart + dateLength + 1;
  const size_t shaEnd = nameLength - suffixLength;
  if (name[dateStart + dateLength] != '-') {
    return false;
  }

  for (size_t i = dateStart; i < dateStart + dateLength; ++i) {
    if (!isDecimalDigit(name[i])) {
      return false;
    }
  }

  for (size_t i = shaStart; i < shaEnd; ++i) {
    if (!isHexDigit(name[i])) {
      return false;
    }
  }

  return true;
}

String findNamedLocalUpdatePath() {
  SpiBusMutex::Guard guard;
  FsFile root = Storage.open("/");
  if (!root || !root.isDirectory()) {
    return "";
  }

  String bestName;
  while (true) {
    FsFile entry = root.openNextFile();
    if (!entry) {
      break;
    }

    char name[96] = "";
    const bool hasName = !entry.isDirectory() && entry.getName(name, sizeof(name));
    entry.close();
    if (hasName && isNamedFirmwareFile(name)) {
      if (bestName.isEmpty() || strcmp(name, bestName.c_str()) > 0) {
        bestName = name;
      }
    }
  }

  root.close();
  return bestName.isEmpty() ? String("") : String("/") + bestName;
}

String findLocalUpdatePath() {
  {
    SpiBusMutex::Guard guard;
    if (Storage.exists(FirmwareUpdateUtil::kFirmwareBinPath)) {
      return FirmwareUpdateUtil::kFirmwareBinPath;
    }
  }

  return findNamedLocalUpdatePath();
}

bool removeLocalUpdateFile(const String& path) {
  SpiBusMutex::Guard guard;
  return Storage.remove(path.c_str());
}

void clearSkippedLocalUpdate() {
  SpiBusMutex::Guard guard;
  if (Storage.exists(kSkippedLocalUpdatePath) && !Storage.remove(kSkippedLocalUpdatePath)) {
    LOG_WRN("FWUPD", "Failed to clear skipped local update marker");
  }
}

bool saveSkippedLocalUpdate(const LocalUpdateFingerprint& fingerprint) {
  SpiBusMutex::Guard guard;
  Storage.mkdir("/.crosspoint");

  FsFile file;
  if (!Storage.openFileForWrite("FWUPD", kSkippedLocalUpdatePath, file)) {
    LOG_ERR("FWUPD", "Failed to open skip marker file for write");
    return false;
  }

  serialization::writePod(file, kSkippedLocalUpdateVersion);
  serialization::writePod(file, fingerprint.fileSize);
  serialization::writePod(file, fingerprint.sampleHash);
  file.close();
  return true;
}

bool loadSkippedLocalUpdate(LocalUpdateFingerprint& fingerprint) {
  FsFile file;
  {
    SpiBusMutex::Guard guard;
    if (!Storage.openFileForRead("FWUPD", kSkippedLocalUpdatePath, file)) {
      return false;
    }
  }

  uint8_t version = 0;
  const bool ok = serialization::readPod(file, version) && version == kSkippedLocalUpdateVersion &&
                  serialization::readPod(file, fingerprint.fileSize) &&
                  serialization::readPod(file, fingerprint.sampleHash);
  file.close();

  if (!ok) {
    LOG_WRN("FWUPD", "Invalid skip marker file, removing it");
    clearSkippedLocalUpdate();
    return false;
  }

  return true;
}

bool hashLocalUpdateSample(const size_t firmwareSize, const size_t offset, LocalUpdateFingerprint& fingerprint,
                           FsFile& file, uint8_t* buffer, const size_t bufferSize) {
  if (!file.seekSet(offset)) {
    LOG_ERR("FWUPD", "Failed to seek firmware file to %zu", offset);
    return false;
  }

  const size_t bytesToRead = firmwareSize > offset ? std::min(bufferSize, firmwareSize - offset) : 0;
  if (bytesToRead == 0) {
    return true;
  }

  const size_t bytesRead = file.read(buffer, bytesToRead);
  if (bytesRead != bytesToRead) {
    LOG_ERR("FWUPD", "Failed to read firmware sample at %zu (%zu / %zu bytes)", offset, bytesRead, bytesToRead);
    return false;
  }

  fingerprint.sampleHash = fnv1aUpdate(fingerprint.sampleHash, buffer, bytesRead);
  return true;
}

bool readFirmwareAppDescription(FsFile& file, esp_app_desc_t& appDesc) {
  const size_t appDescOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
  if (!file.seekSet(appDescOffset)) {
    LOG_ERR("FWUPD", "Failed to seek firmware file to app description");
    return false;
  }

  const size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(&appDesc), sizeof(appDesc));
  if (bytesRead != sizeof(appDesc)) {
    LOG_ERR("FWUPD", "Failed to read app description (%zu / %zu bytes)", bytesRead, sizeof(appDesc));
    return false;
  }

  if (appDesc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
    LOG_WRN("FWUPD", "Firmware file missing valid app description");
    return false;
  }

  return true;
}

bool readCrossPointVersionMarker(FsFile& file, String& version) {
  constexpr char userAgentMarker[] = "CrossPoint-ESP32-";
  constexpr char bootLogMarker[] = "Starting CrossPoint version ";

  if (!file.seekSet(0)) {
    LOG_ERR("FWUPD", "Failed to seek firmware file for version scan");
    return false;
  }

  ScopedBuffer buffer(kVersionScanChunkBytes);
  if (!buffer) {
    LOG_ERR("FWUPD", "Failed to allocate version scan buffer");
    return false;
  }

  size_t userAgentMatchLength = 0;
  size_t bootLogMatchLength = 0;
  bool readingVersion = false;
  version = "";

  while (true) {
    const size_t bytesRead = file.read(buffer.data(), kVersionScanChunkBytes);
    if (bytesRead == 0) {
      break;
    }

    for (size_t i = 0; i < bytesRead; ++i) {
      const char ch = static_cast<char>(buffer.data()[i]);
      if (readingVersion) {
        if (ch != '\0' && isFirmwareVersionChar(ch) && version.length() < kMaxFirmwareVersionLength) {
          version += ch;
          continue;
        }
        version.trim();
        return !version.isEmpty();
      }

      advanceMarkerMatch(ch, userAgentMarker, userAgentMatchLength);
      advanceMarkerMatch(ch, bootLogMarker, bootLogMatchLength);
      if (userAgentMarker[userAgentMatchLength] == '\0' || bootLogMarker[bootLogMatchLength] == '\0') {
        readingVersion = true;
        version = "";
        userAgentMatchLength = 0;
        bootLogMatchLength = 0;
      }
    }
  }

  version.trim();
  return !version.isEmpty();
}

bool computeLocalUpdateFingerprint(const String& path, LocalUpdateFingerprint& fingerprint) {
  FsFile file;
  {
    SpiBusMutex::Guard guard;
    if (!Storage.openFileForRead("FWUPD", path, file)) {
      LOG_ERR("FWUPD", "Failed to open firmware file for fingerprint");
      return false;
    }
    fingerprint.fileSize = static_cast<uint32_t>(file.size());
  }

  fingerprint.sampleHash = 2166136261u;
  fingerprint.sampleHash = fnv1aUpdate(fingerprint.sampleHash, reinterpret_cast<const uint8_t*>(&fingerprint.fileSize),
                                       sizeof(fingerprint.fileSize));

  ScopedBuffer buffer(kFingerprintSampleBytes);
  if (!buffer) {
    LOG_ERR("FWUPD", "Failed to allocate fingerprint buffer");
    file.close();
    return false;
  }

  std::array<size_t, 3> offsets = {
      0, fingerprint.fileSize > kFingerprintSampleBytes ? (fingerprint.fileSize - kFingerprintSampleBytes) / 2 : 0,
      fingerprint.fileSize > kFingerprintSampleBytes
          ? static_cast<size_t>(fingerprint.fileSize) - kFingerprintSampleBytes
          : 0};

  for (size_t i = 0; i < offsets.size(); ++i) {
    const size_t offset = offsets[i];
    bool alreadyHasOffset = false;
    for (size_t j = 0; j < i; ++j) {
      if (offsets[j] == offset) {
        alreadyHasOffset = true;
        break;
      }
    }
    if (alreadyHasOffset) {
      continue;
    }
    if (!hashLocalUpdateSample(fingerprint.fileSize, offset, fingerprint, file, buffer.data(),
                               kFingerprintSampleBytes)) {
      file.close();
      return false;
    }
  }

  file.close();
  return true;
}

bool readLocalUpdateVersion(const String& path, String& version) {
  FsFile file;
  {
    SpiBusMutex::Guard guard;
    if (!Storage.openFileForRead("FWUPD", path, file)) {
      LOG_ERR("FWUPD", "Failed to open firmware file for version read");
      return false;
    }
  }

  if (readCrossPointVersionMarker(file, version)) {
    file.close();
    return true;
  }

  esp_app_desc_t appDesc{};
  const bool ok = readFirmwareAppDescription(file, appDesc);
  file.close();
  if (!ok) {
    return false;
  }

  version = appDesc.version;
  version.trim();
  return !version.isEmpty();
}

bool loadLocalUpdateMetadata(LocalUpdateMetadata& metadata) {
  metadata.path = findLocalUpdatePath();
  if (metadata.path.isEmpty()) {
    return false;
  }

  if (!computeLocalUpdateFingerprint(metadata.path, metadata.fingerprint)) {
    return false;
  }

  if (!readLocalUpdateVersion(metadata.path, metadata.candidateVersion)) {
    metadata.candidateVersion = "Unknown";
  }

  return true;
}

bool isCurrentLocalUpdateSkipped(const LocalUpdateMetadata& currentMetadata) {
  LocalUpdateFingerprint skippedFingerprint;
  if (!loadSkippedLocalUpdate(skippedFingerprint)) {
    return false;
  }

  return skippedFingerprint.fileSize == currentMetadata.fingerprint.fileSize &&
         skippedFingerprint.sampleHash == currentMetadata.fingerprint.sampleHash;
}

const char* promptTitle(const int index) {
  switch (static_cast<LocalUpdatePromptAction>(index)) {
    case LocalUpdatePromptAction::Install:
      return tr(STR_INSTALL);
    case LocalUpdatePromptAction::SkipForNow:
      return tr(STR_SKIP_FOR_NOW);
    case LocalUpdatePromptAction::SkipThisVersion:
      return tr(STR_SKIP_THIS_VERSION);
    case LocalUpdatePromptAction::DeleteIt:
      return tr(STR_DELETE_IT);
  }

  return "";
}

String formatFirmwareSize(const uint32_t bytes) {
  if (bytes >= 1024 * 1024) {
    return String(static_cast<unsigned long>(bytes / (1024 * 1024))) + " MB";
  }
  if (bytes >= 1024) {
    return String(static_cast<unsigned long>(bytes / 1024)) + " KB";
  }
  return String(static_cast<unsigned long>(bytes)) + " B";
}

void renderLocalUpdatePrompt(GfxRenderer& renderer, const MappedInputManager& mappedInput, const int selectedIndex,
                             const LocalUpdateMetadata& metadata) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE));

  int infoY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 4;
  renderer.drawCenteredText(UI_10_FONT_ID, infoY, tr(STR_LOCAL_UPDATE_FOUND), true, EpdFontFamily::BOLD);
  infoY += renderer.getLineHeight(UI_10_FONT_ID) + 6;
  renderer.drawCenteredText(UI_10_FONT_ID, infoY, tr(STR_LOCAL_UPDATE_PROMPT), true);
  infoY += renderer.getLineHeight(UI_10_FONT_ID) + 6;
  renderer.drawCenteredText(UI_10_FONT_ID, infoY, formatFirmwareSize(metadata.fingerprint.fileSize).c_str(), true);
  infoY += renderer.getLineHeight(UI_10_FONT_ID) + 6;

  String currentVersion = CROSSPOINT_VERSION;
  currentVersion.trim();
  const String currentVersionLine = String(tr(STR_CURRENT_VERSION)) + currentVersion;
  const String newVersionLine = String(tr(STR_NEW_VERSION)) + metadata.candidateVersion;
  renderer.drawCenteredText(UI_10_FONT_ID, infoY, currentVersionLine.c_str(), true);
  infoY += renderer.getLineHeight(UI_10_FONT_ID) + 4;
  renderer.drawCenteredText(UI_10_FONT_ID, infoY, newVersionLine.c_str(), true);

  const int contentTop = infoY + renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing + 8;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, kPromptItemCount, selectedIndex,
               [](const int index) { return std::string(promptTitle(index)); });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

LocalUpdatePromptAction promptForLocalUpdate(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const LocalUpdateMetadata& metadata) {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  mappedInput.clearTransientState();
  renderLocalUpdatePrompt(renderer, mappedInput, selectedIndex, metadata);

  while (true) {
    mappedInput.update();

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      return LocalUpdatePromptAction::SkipForNow;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      return static_cast<LocalUpdatePromptAction>(selectedIndex);
    }

    bool selectionChanged = false;
    buttonNavigator.onNextRelease([&] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, kPromptItemCount);
      selectionChanged = true;
    });
    buttonNavigator.onPreviousRelease([&] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, kPromptItemCount);
      selectionChanged = true;
    });

    if (selectionChanged) {
      renderLocalUpdatePrompt(renderer, mappedInput, selectedIndex, metadata);
    }

    delay(20);
  }
}
}  // namespace

bool FirmwareUpdateUtil::checkForLocalUpdate() { return !findLocalUpdatePath().isEmpty(); }

void FirmwareUpdateUtil::handleLocalUpdateBootFlow(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (!checkForLocalUpdate()) {
    return;
  }

  LocalUpdateMetadata metadata;
  if (!loadLocalUpdateMetadata(metadata)) {
    return;
  }

  if (isCurrentLocalUpdateSkipped(metadata)) {
    LOG_INF("FWUPD", "Skipping boot prompt for previously skipped firmware: %s", metadata.path.c_str());
    return;
  }

  switch (promptForLocalUpdate(renderer, mappedInput, metadata)) {
    case LocalUpdatePromptAction::Install:
      clearSkippedLocalUpdate();
      performLocalUpdate(renderer);
      break;
    case LocalUpdatePromptAction::SkipForNow:
      LOG_INF("FWUPD", "User skipped local update for now");
      break;
    case LocalUpdatePromptAction::SkipThisVersion:
      if (saveSkippedLocalUpdate(metadata.fingerprint)) {
        LOG_INF("FWUPD", "User skipped this local firmware version");
      }
      break;
    case LocalUpdatePromptAction::DeleteIt:
      clearSkippedLocalUpdate();
      if (!removeLocalUpdateFile(metadata.path)) {
        LOG_ERR("FWUPD", "Failed to delete %s", metadata.path.c_str());
      }
      break;
  }
}

bool FirmwareUpdateUtil::performLocalUpdate(const GfxRenderer& renderer) {
  const String firmwarePath = findLocalUpdatePath();
  if (firmwarePath.isEmpty()) {
    LOG_ERR("FWUPD", "No local firmware file found");
    return false;
  }

  LOG_INF("FWUPD", "Starting local firmware update from %s", firmwarePath.c_str());

  FsFile firmwareFile;
  size_t firmwareSize = 0;
  {
    SpiBusMutex::Guard guard;
    if (!Storage.openFileForRead("FWUPD", firmwarePath, firmwareFile)) {
      LOG_ERR("FWUPD", "Failed to open firmware file");
      return false;
    }
    firmwareSize = firmwareFile.size();
  }

  if (firmwareSize == 0) {
    LOG_ERR("FWUPD", "Firmware file is empty");
    SpiBusMutex::Guard guard;
    firmwareFile.close();
    return false;
  }

  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    LOG_ERR("FWUPD", "No OTA partition available");
    SpiBusMutex::Guard guard;
    firmwareFile.close();
    return false;
  }

  if (firmwareSize > updatePartition->size) {
    LOG_ERR("FWUPD", "Firmware file size (%zu) exceeds partition size (%zu)", firmwareSize, updatePartition->size);
    SpiBusMutex::Guard guard;
    firmwareFile.close();
    return false;
  }

  esp_ota_handle_t updateHandle = 0;
  // OTA_WITH_SEQUENTIAL_WRITES: erase sectors on demand
  esp_err_t err = esp_ota_begin(updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &updateHandle);
  if (err != ESP_OK) {
    LOG_ERR("FWUPD", "esp_ota_begin failed: %s", esp_err_to_name(err));
    SpiBusMutex::Guard guard;
    firmwareFile.close();
    return false;
  }

  const size_t bufferSize = 4096;
  ScopedBuffer buffer(bufferSize);
  if (!buffer) {
    LOG_ERR("FWUPD", "Failed to allocate buffer");
    esp_ota_abort(updateHandle);
    SpiBusMutex::Guard guard;
    firmwareFile.close();
    return false;
  }

  size_t totalBytesWritten = 0;
  int lastProgress = -1;

  while (totalBytesWritten < firmwareSize) {
    size_t bytesRead;
    {
      SpiBusMutex::Guard guard;
      bytesRead = firmwareFile.read(buffer.data(), bufferSize);
    }

    if (bytesRead == 0) break;

    err = esp_ota_write(updateHandle, buffer.data(), bytesRead);
    if (err != ESP_OK) {
      LOG_ERR("FWUPD", "esp_ota_write failed: %s", esp_err_to_name(err));
      esp_ota_abort(updateHandle);
      SpiBusMutex::Guard guard;
      firmwareFile.close();
      return false;
    }

    totalBytesWritten += bytesRead;

    int progress = (totalBytesWritten * 100) / firmwareSize;
    if (progress != lastProgress) {
      lastProgress = progress;
      LOG_INF("FWUPD", "Progress: %d%%", progress);

      renderer.clearScreen();
      renderer.drawCenteredText(UI_12_FONT_ID, 200, tr(STR_UPDATING), true, EpdFontFamily::BOLD);
      char progressText[32];
      snprintf(progressText, sizeof(progressText), "%d%%", progress);
      renderer.drawCenteredText(UI_10_FONT_ID, 240, progressText, true);

      const int barWidth = 300;
      const int barHeight = 20;
      const int barX = (renderer.getScreenWidth() - barWidth) / 2;
      const int barY = 270;
      renderer.drawRect(barX, barY, barWidth, barHeight);
      renderer.fillRect(barX, barY, (barWidth * progress) / 100, barHeight);

      renderer.displayBuffer();
    }
  }

  {
    SpiBusMutex::Guard guard;
    firmwareFile.close();
  }

  if (totalBytesWritten != firmwareSize) {
    LOG_ERR("FWUPD", "Firmware write incomplete: %zu / %zu", totalBytesWritten, firmwareSize);
    esp_ota_abort(updateHandle);
    return false;
  }

  err = esp_ota_end(updateHandle);
  if (err != ESP_OK) {
    LOG_ERR("FWUPD", "esp_ota_end failed: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_ota_set_boot_partition(updatePartition);
  if (err != ESP_OK) {
    LOG_ERR("FWUPD", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
    return false;
  }

  LOG_INF("FWUPD", "Firmware update successful. Deleting %s and rebooting...", firmwarePath.c_str());

  clearSkippedLocalUpdate();
  {
    SpiBusMutex::Guard guard;
    Storage.remove(firmwarePath.c_str());
  }

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 200, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, 240, tr(STR_BOOTING), true);
  renderer.displayBuffer();

  delay(2000);
  esp_restart();

  return true;
}
