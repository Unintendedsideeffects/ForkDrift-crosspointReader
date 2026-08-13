#include "features/terminus_sleep/RefreshEvidence.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

namespace features::terminus_sleep {
namespace {

constexpr char kEvidencePath[] = "/.crosspoint/terminus-refresh-evidence.json";
constexpr char kEvidenceTempPath[] = "/.crosspoint/terminus-refresh-evidence.json.tmp";
constexpr char kEvidenceBackupPath[] = "/.crosspoint/terminus-refresh-evidence.json.bak";
constexpr size_t kMaxEvidenceBytes = 4096;

void evidenceToDoc(const RefreshEvidence& evidence, JsonDocument& doc) {
  doc["schema_version"] = evidence.schemaVersion;
  doc["timer_wake_count"] = evidence.timerWakeCount;
  doc["completed_cycle_count"] = evidence.completedCycleCount;
  doc["wifi_success_count"] = evidence.wifiSuccessCount;
  doc["fetch_success_count"] = evidence.fetchSuccessCount;
  doc["render_success_count"] = evidence.renderSuccessCount;
  doc["verification_target_completed_cycle_count"] = evidence.verificationTargetCompletedCycleCount;
  doc["verification_rendezvous_count"] = evidence.verificationRendezvousCount;
  doc["last_stage"] = evidence.lastCycleComplete ? "complete" : (evidence.timerWakeCount > 0 ? "started" : "never");
  doc["last_cycle_complete"] = evidence.lastCycleComplete;
  doc["last_wifi_started"] = evidence.lastWifiStarted;
  doc["last_wifi_connected"] = evidence.lastWifiConnected;
  doc["last_fetch_attempted"] = evidence.lastFetchAttempted;
  doc["last_fetch_ok"] = evidence.lastFetchOk;
  doc["last_used_stale_image"] = evidence.lastUsedStaleImage;
  doc["last_render_completed"] = evidence.lastRenderCompleted;
  doc["last_rearm_armed"] = evidence.lastRearmArmed;
  doc["last_render_stage"] = evidence.lastRenderStage;
  doc["last_render_free_heap"] = evidence.lastRenderFreeHeap;
  doc["last_render_max_alloc_heap"] = evidence.lastRenderMaxAllocHeap;
  doc["last_wake_epoch"] = evidence.lastWakeEpoch;
  doc["last_completed_epoch"] = evidence.lastCompletedEpoch;
  doc["last_rearm_interval_seconds"] = evidence.lastRearmIntervalSeconds;
  doc["last_server_refresh_seconds"] = evidence.lastServerRefreshSeconds;
}

bool docToEvidence(const JsonDocument& doc, RefreshEvidence& evidence) {
  const uint8_t version = doc["schema_version"] | static_cast<uint8_t>(0);
  if (version != RefreshEvidence::kSchemaVersion) {
    return false;
  }

  RefreshEvidence loaded;
  loaded.schemaVersion = version;
  loaded.timerWakeCount = doc["timer_wake_count"] | 0U;
  loaded.completedCycleCount = doc["completed_cycle_count"] | 0U;
  loaded.wifiSuccessCount = doc["wifi_success_count"] | 0U;
  loaded.fetchSuccessCount = doc["fetch_success_count"] | 0U;
  loaded.renderSuccessCount = doc["render_success_count"] | 0U;
  loaded.verificationTargetCompletedCycleCount = doc["verification_target_completed_cycle_count"] | 0U;
  loaded.verificationRendezvousCount = doc["verification_rendezvous_count"] | 0U;
  loaded.lastCycleComplete = doc["last_cycle_complete"] | false;
  loaded.lastWifiStarted = doc["last_wifi_started"] | false;
  loaded.lastWifiConnected = doc["last_wifi_connected"] | false;
  loaded.lastFetchAttempted = doc["last_fetch_attempted"] | false;
  loaded.lastFetchOk = doc["last_fetch_ok"] | false;
  loaded.lastUsedStaleImage = doc["last_used_stale_image"] | false;
  loaded.lastRenderCompleted = doc["last_render_completed"] | false;
  loaded.lastRearmArmed = doc["last_rearm_armed"] | false;
  loaded.lastRenderStage = doc["last_render_stage"] | "not-attempted";
  loaded.lastRenderFreeHeap = doc["last_render_free_heap"] | 0U;
  loaded.lastRenderMaxAllocHeap = doc["last_render_max_alloc_heap"] | 0U;
  loaded.lastWakeEpoch = doc["last_wake_epoch"] | 0U;
  loaded.lastCompletedEpoch = doc["last_completed_epoch"] | 0U;
  loaded.lastRearmIntervalSeconds = doc["last_rearm_interval_seconds"] | 0U;
  loaded.lastServerRefreshSeconds = doc["last_server_refresh_seconds"] | 0U;

  // These invariants make a partially edited or nonsensical record visible
  // instead of allowing it to satisfy the machine acceptance gate.
  if (loaded.completedCycleCount > loaded.timerWakeCount || loaded.wifiSuccessCount > loaded.timerWakeCount ||
      loaded.fetchSuccessCount > loaded.timerWakeCount || loaded.renderSuccessCount > loaded.timerWakeCount) {
    return false;
  }
  evidence = loaded;
  return true;
}

bool loadPath(const char* path, RefreshEvidence& evidence) {
  if (!Storage.exists(path)) {
    return false;
  }
  HalFile file;
  if (!Storage.openFileForRead("TRMNL_EVIDENCE", path, file) || file.size() > kMaxEvidenceBytes) {
    return false;
  }
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  return !error && docToEvidence(doc, evidence);
}

}  // namespace

bool loadRefreshEvidence(RefreshEvidence& evidence) {
  if (loadPath(kEvidencePath, evidence)) {
    // Interrupted-save debris is safe to discard once the primary is valid.
    if (Storage.exists(kEvidenceTempPath)) Storage.remove(kEvidenceTempPath);
    if (Storage.exists(kEvidenceBackupPath)) Storage.remove(kEvidenceBackupPath);
    return true;
  }

  const char* recoveryPath = nullptr;
  if (loadPath(kEvidenceTempPath, evidence)) {
    recoveryPath = kEvidenceTempPath;
  } else if (loadPath(kEvidenceBackupPath, evidence)) {
    recoveryPath = kEvidenceBackupPath;
  }
  if (!recoveryPath) {
    return false;
  }

  if (Storage.exists(kEvidencePath)) Storage.remove(kEvidencePath);
  if (!Storage.rename(recoveryPath, kEvidencePath)) {
    LOG_WRN("TRMNL", "Loaded refresh evidence but could not promote recovery copy");
  }
  return true;
}

bool saveRefreshEvidence(const RefreshEvidence& evidence) {
  JsonDocument doc;
  evidenceToDoc(evidence, doc);
  String json;
  serializeJson(doc, json);

  Storage.mkdir("/.crosspoint");
  if (Storage.exists(kEvidenceTempPath)) Storage.remove(kEvidenceTempPath);
  if (!Storage.writeFile(kEvidenceTempPath, json)) {
    LOG_ERR("TRMNL", "Failed to write timed-refresh evidence temp file");
    return false;
  }

  if (Storage.exists(kEvidenceBackupPath)) Storage.remove(kEvidenceBackupPath);
  const bool hadPrimary = Storage.exists(kEvidencePath);
  if (hadPrimary && !Storage.rename(kEvidencePath, kEvidenceBackupPath)) {
    LOG_ERR("TRMNL", "Failed to back up timed-refresh evidence");
    Storage.remove(kEvidenceTempPath);
    return false;
  }
  if (!Storage.rename(kEvidenceTempPath, kEvidencePath)) {
    LOG_ERR("TRMNL", "Failed to promote timed-refresh evidence");
    if (hadPrimary) Storage.rename(kEvidenceBackupPath, kEvidencePath);
    return false;
  }
  if (Storage.exists(kEvidenceBackupPath)) Storage.remove(kEvidenceBackupPath);
  return true;
}

void beginRefreshCycle(RefreshEvidence& evidence, const uint32_t wakeEpoch) {
  evidence.schemaVersion = RefreshEvidence::kSchemaVersion;
  evidence.timerWakeCount++;
  evidence.lastCycleComplete = false;
  evidence.lastWifiStarted = false;
  evidence.lastWifiConnected = false;
  evidence.lastFetchAttempted = false;
  evidence.lastFetchOk = false;
  evidence.lastUsedStaleImage = false;
  evidence.lastRenderCompleted = false;
  evidence.lastRearmArmed = false;
  evidence.lastRenderStage = "not-attempted";
  evidence.lastRenderFreeHeap = 0;
  evidence.lastRenderMaxAllocHeap = 0;
  evidence.lastWakeEpoch = wakeEpoch;
  evidence.lastCompletedEpoch = 0;
  evidence.lastRearmIntervalSeconds = 0;
  evidence.lastServerRefreshSeconds = 0;
}

void completeRefreshCycle(RefreshEvidence& evidence, const RefreshCycleOutcome& outcome) {
  evidence.lastCycleComplete = true;
  evidence.lastWifiStarted = outcome.wifiStarted;
  evidence.lastWifiConnected = outcome.wifiConnected;
  evidence.lastFetchAttempted = outcome.fetchAttempted;
  evidence.lastFetchOk = outcome.fetchOk;
  evidence.lastUsedStaleImage = outcome.usedStaleImage;
  evidence.lastRenderCompleted = outcome.renderCompleted;
  evidence.lastRearmArmed = outcome.rearmArmed;
  evidence.lastRenderStage = outcome.renderStage;
  evidence.lastRenderFreeHeap = outcome.renderFreeHeap;
  evidence.lastRenderMaxAllocHeap = outcome.renderMaxAllocHeap;
  evidence.lastCompletedEpoch = outcome.completedEpoch;
  evidence.lastRearmIntervalSeconds = outcome.rearmIntervalSeconds;
  evidence.lastServerRefreshSeconds = outcome.serverRefreshSeconds;
  evidence.completedCycleCount++;
  if (outcome.wifiConnected) evidence.wifiSuccessCount++;
  if (outcome.fetchOk) evidence.fetchSuccessCount++;
  if (outcome.renderCompleted) evidence.renderSuccessCount++;
}

uint32_t armRefreshVerification(RefreshEvidence& evidence, const uint32_t additionalCycles) {
  if (additionalCycles == 0 || UINT32_MAX - evidence.completedCycleCount < additionalCycles) {
    evidence.verificationTargetCompletedCycleCount = 0;
    return 0;
  }
  evidence.verificationTargetCompletedCycleCount = evidence.completedCycleCount + additionalCycles;
  return evidence.verificationTargetCompletedCycleCount;
}

bool consumeVerificationRendezvous(RefreshEvidence& evidence) {
  const uint32_t target = evidence.verificationTargetCompletedCycleCount;
  if (target == 0 || evidence.completedCycleCount < target) {
    return false;
  }
  evidence.verificationTargetCompletedCycleCount = 0;
  evidence.verificationRendezvousCount++;
  return true;
}

std::string refreshEvidenceJson(const RefreshEvidence& evidence) {
  JsonDocument doc;
  evidenceToDoc(evidence, doc);
  std::string json;
  serializeJson(doc, json);
  return json;
}

}  // namespace features::terminus_sleep
