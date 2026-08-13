#include <ArduinoJson.h>
#include <HalStorage.h>

#include "doctest/doctest.h"
#include "src/features/terminus_sleep/RefreshEvidence.h"

using features::terminus_sleep::armRefreshVerification;
using features::terminus_sleep::beginRefreshCycle;
using features::terminus_sleep::completeRefreshCycle;
using features::terminus_sleep::consumeVerificationRendezvous;
using features::terminus_sleep::loadRefreshEvidence;
using features::terminus_sleep::RefreshCycleOutcome;
using features::terminus_sleep::RefreshEvidence;
using features::terminus_sleep::refreshEvidenceJson;
using features::terminus_sleep::saveRefreshEvidence;

TEST_CASE("Terminus refresh evidence records a complete successful cycle") {
  RefreshEvidence evidence;
  beginRefreshCycle(evidence, 1'723'456'789U);
  CHECK(evidence.timerWakeCount == 1);
  CHECK_FALSE(evidence.lastCycleComplete);

  completeRefreshCycle(evidence, RefreshCycleOutcome{
                                     .wifiStarted = true,
                                     .wifiConnected = true,
                                     .fetchAttempted = true,
                                     .fetchOk = true,
                                     .usedStaleImage = false,
                                     .renderCompleted = true,
                                     .rearmArmed = true,
                                     .renderStage = "complete",
                                     .renderFreeHeap = 72'000,
                                     .renderMaxAllocHeap = 64'000,
                                     .completedEpoch = 1'723'456'794U,
                                     .rearmIntervalSeconds = 210,
                                     .serverRefreshSeconds = 210,
                                 });

  CHECK(evidence.completedCycleCount == 1);
  CHECK(evidence.wifiSuccessCount == 1);
  CHECK(evidence.fetchSuccessCount == 1);
  CHECK(evidence.renderSuccessCount == 1);
  CHECK(evidence.lastCycleComplete);
  CHECK(evidence.lastRearmArmed);
  CHECK(evidence.lastRenderStage == "complete");
  CHECK(evidence.lastRenderFreeHeap == 72'000);
  CHECK(evidence.lastRenderMaxAllocHeap == 64'000);
  CHECK(evidence.lastRearmIntervalSeconds == 210);
}

TEST_CASE("Terminus refresh evidence preserves failure diagnostics without success counters") {
  RefreshEvidence evidence;
  beginRefreshCycle(evidence, 100);
  completeRefreshCycle(evidence, RefreshCycleOutcome{
                                     .wifiStarted = true,
                                     .wifiConnected = false,
                                     .fetchAttempted = false,
                                     .fetchOk = false,
                                     .usedStaleImage = true,
                                     .renderCompleted = true,
                                     .rearmArmed = true,
                                     .completedEpoch = 101,
                                     .rearmIntervalSeconds = 900,
                                     .serverRefreshSeconds = 900,
                                 });

  CHECK(evidence.completedCycleCount == 1);
  CHECK(evidence.wifiSuccessCount == 0);
  CHECK(evidence.fetchSuccessCount == 0);
  CHECK(evidence.renderSuccessCount == 1);
  CHECK(evidence.lastUsedStaleImage);
  CHECK_FALSE(evidence.lastFetchAttempted);
}

TEST_CASE("Terminus refresh evidence round trips through recoverable SD storage") {
  Storage.reset();
  RefreshEvidence saved;
  saved.timerWakeCount = 3;
  saved.completedCycleCount = 3;
  saved.wifiSuccessCount = 3;
  saved.fetchSuccessCount = 3;
  saved.renderSuccessCount = 3;
  saved.lastCycleComplete = true;
  saved.lastFetchOk = true;
  saved.lastRenderCompleted = true;
  saved.lastRearmArmed = true;
  saved.lastRenderStage = "complete";
  saved.lastRenderFreeHeap = 70'000;
  saved.lastRenderMaxAllocHeap = 62'000;
  saved.lastRearmIntervalSeconds = 210;

  REQUIRE(saveRefreshEvidence(saved));
  RefreshEvidence loaded;
  REQUIRE(loadRefreshEvidence(loaded));
  CHECK(loaded.timerWakeCount == 3);
  CHECK(loaded.fetchSuccessCount == 3);
  CHECK(loaded.lastRearmIntervalSeconds == 210);
  CHECK(loaded.lastRenderStage == "complete");
  CHECK(loaded.lastRenderFreeHeap == 70'000);
  CHECK(loaded.lastRenderMaxAllocHeap == 62'000);
  CHECK_FALSE(Storage.exists("/.crosspoint/terminus-refresh-evidence.json.tmp"));
  CHECK_FALSE(Storage.exists("/.crosspoint/terminus-refresh-evidence.json.bak"));
}

TEST_CASE("Terminus refresh evidence rejects impossible counters") {
  Storage.reset();
  REQUIRE(Storage.writeFile("/.crosspoint/terminus-refresh-evidence.json",
                            R"({"schema_version":1,"timer_wake_count":1,"completed_cycle_count":2})"));
  RefreshEvidence evidence;
  CHECK_FALSE(loadRefreshEvidence(evidence));
}

TEST_CASE("Terminus refresh evidence JSON is stable and machine readable") {
  RefreshEvidence evidence;
  beginRefreshCycle(evidence, 42);
  JsonDocument doc;
  REQUIRE_FALSE(deserializeJson(doc, refreshEvidenceJson(evidence)));
  CHECK(doc["schema_version"].as<int>() == 1);
  CHECK(doc["timer_wake_count"].as<int>() == 1);
  CHECK(std::string(doc["last_stage"].as<const char*>()) == "started");
}

TEST_CASE("Terminus verification rendezvous is bounded and consumed after completed cycles") {
  RefreshEvidence evidence;
  evidence.completedCycleCount = 7;
  CHECK(armRefreshVerification(evidence, 2) == 9);
  CHECK(evidence.verificationTargetCompletedCycleCount == 9);
  CHECK_FALSE(consumeVerificationRendezvous(evidence));

  evidence.completedCycleCount = 8;
  CHECK_FALSE(consumeVerificationRendezvous(evidence));
  evidence.completedCycleCount = 9;
  CHECK(consumeVerificationRendezvous(evidence));
  CHECK(evidence.verificationTargetCompletedCycleCount == 0);
  CHECK(evidence.verificationRendezvousCount == 1);
  CHECK_FALSE(consumeVerificationRendezvous(evidence));
}

TEST_CASE("Terminus verification rendezvous rejects zero and overflow") {
  RefreshEvidence evidence;
  CHECK(armRefreshVerification(evidence, 0) == 0);
  evidence.completedCycleCount = UINT32_MAX - 1;
  CHECK(armRefreshVerification(evidence, 2) == 0);
}
