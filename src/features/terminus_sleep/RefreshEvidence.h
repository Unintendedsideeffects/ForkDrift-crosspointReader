#pragma once

#include <cstdint>
#include <string>

namespace features::terminus_sleep {

// Durable, non-secret proof of unattended Terminus timer-refresh behavior.
// The counters survive deep sleep on the SD card; the last-cycle fields make
// failures diagnosable after USB CDC has disappeared and later reattached.
struct RefreshEvidence {
  static constexpr uint8_t kSchemaVersion = 1;

  uint8_t schemaVersion = kSchemaVersion;
  uint32_t timerWakeCount = 0;
  uint32_t completedCycleCount = 0;
  uint32_t wifiSuccessCount = 0;
  uint32_t fetchSuccessCount = 0;
  uint32_t renderSuccessCount = 0;
  uint32_t verificationTargetCompletedCycleCount = 0;
  uint32_t verificationRendezvousCount = 0;

  bool lastCycleComplete = false;
  bool lastWifiStarted = false;
  bool lastWifiConnected = false;
  bool lastFetchAttempted = false;
  bool lastFetchOk = false;
  bool lastUsedStaleImage = false;
  bool lastRenderCompleted = false;
  bool lastRearmArmed = false;
  std::string lastRenderStage = "not-attempted";
  uint32_t lastRenderFreeHeap = 0;
  uint32_t lastRenderMaxAllocHeap = 0;
  uint32_t lastWakeEpoch = 0;
  uint32_t lastCompletedEpoch = 0;
  uint32_t lastRearmIntervalSeconds = 0;
  uint32_t lastServerRefreshSeconds = 0;
};

struct RefreshCycleOutcome {
  bool wifiStarted = false;
  bool wifiConnected = false;
  bool fetchAttempted = false;
  bool fetchOk = false;
  bool usedStaleImage = false;
  bool renderCompleted = false;
  bool rearmArmed = false;
  std::string renderStage = "not-attempted";
  uint32_t renderFreeHeap = 0;
  uint32_t renderMaxAllocHeap = 0;
  uint32_t completedEpoch = 0;
  uint32_t rearmIntervalSeconds = 0;
  uint32_t serverRefreshSeconds = 0;
};

// Loads the newest valid record. A missing record is not an error for callers:
// pass a default-constructed RefreshEvidence when this returns false.
bool loadRefreshEvidence(RefreshEvidence& evidence);

// Recoverable temp/backup promotion; never writes credentials or image URLs.
bool saveRefreshEvidence(const RefreshEvidence& evidence);

// Mutators are separated from storage so their counter semantics are host-testable.
void beginRefreshCycle(RefreshEvidence& evidence, uint32_t wakeEpoch);
void completeRefreshCycle(RefreshEvidence& evidence, const RefreshCycleOutcome& outcome);

// Arms a bounded hardware-test rendezvous. Normal firmware behavior is
// unchanged unless the serial harness explicitly sets a target. Once that many
// additional timer cycles have completed, consumeVerificationRendezvous()
// clears the target and tells the caller to restart into an awake boot so the
// host can retrieve the durable evidence.
uint32_t armRefreshVerification(RefreshEvidence& evidence, uint32_t additionalCycles);
bool consumeVerificationRendezvous(RefreshEvidence& evidence);

std::string refreshEvidenceJson(const RefreshEvidence& evidence);

}  // namespace features::terminus_sleep
