#pragma once

#include <Arduino.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class OtaUpdater {
 public:
  struct FeatureStoreEntry {
    String id;
    String displayName;
    String version;
    String featureFlags;
    String downloadUrl;
    String checksum;
    size_t binarySize = 0;
    bool compatible = true;
    String compatibilityError;
  };

 private:
  bool updateAvailable = false;
  std::string latestVersion;
  std::string otaUrl;
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;
  bool render = false;
  bool factoryResetOnInstall = false;
  const std::atomic<bool>* cancelRequested = nullptr;
  std::vector<FeatureStoreEntry> featureStoreEntries;
  String selectedBundleId;
  String selectedFeatureFlags;
  String selectedChecksum;
  String lastError;

 public:
  using ProgressCallback = void (*)(void* ctx);

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
  };

  static constexpr const char* CATALOG_UNAVAILABLE_ERROR = "Feature store catalog unavailable";
  static constexpr const char* BUNDLE_UNAVAILABLE_ERROR = "Selected bundle unavailable";
  static constexpr const char* INCOMPATIBLE_BUNDLE_ERROR = "Selected bundle incompatible with this device";

  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize; }

  size_t getTotalSize() const { return totalSize; }

  bool getRender() const { return render; }

  bool willFactoryResetOnInstall() const { return factoryResetOnInstall; }

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  OtaUpdaterError checkForUpdate();
  OtaUpdaterError installUpdate();
  bool loadFeatureStoreCatalog();
  bool hasFeatureStoreCatalog() const;
  const std::vector<FeatureStoreEntry>& getFeatureStoreEntries() const;
  bool selectFeatureStoreBundleByIndex(size_t index);
  const String& getLastError() const;
  void setCancelFlag(const std::atomic<bool>* cancelFlag) { cancelRequested = cancelFlag; }
};
