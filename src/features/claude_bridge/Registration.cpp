#include "features/claude_bridge/Registration.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FeatureFlags.h>
#include <Logging.h>
#include <WebServer.h>
#ifndef SIMULATOR
#include <esp_random.h>
#endif

#include <algorithm>

#include "activities/util/ClaudeBridgeActivity.h"
#include "core/features/FeatureCatalog.h"
#include "core/features/FeatureModules.h"
#include "core/registries/HomeActionRegistry.h"
#include "core/registries/LifecycleRegistry.h"
#include "core/registries/WebRouteRegistry.h"
#include "util/ClaudeBridgeStore.h"

namespace features::claude_bridge {

#if ENABLE_CLAUDE_BRIDGE
namespace {

constexpr size_t kMaxQuestions = 4;
constexpr size_t kMaxOptions = 4;
constexpr size_t kMaxQuestionChars = 512;
constexpr size_t kMaxHeaderChars = 64;
constexpr size_t kMaxOptionLabelChars = 96;
constexpr size_t kMaxOptionDescriptionChars = 256;
constexpr size_t kMaxSessionTitleChars = 96;
constexpr size_t kMaxCwdChars = 256;
constexpr uint32_t kRequestTtlMs = 5UL * 60UL * 1000UL;

enum class SlotState : uint8_t { Empty, Pending, Answered };

struct Slot {
  SlotState state = SlotState::Empty;
  PendingRequest request;
  std::vector<QuestionAnswer> answers;
  uint32_t createdAtMs = 0;
};

SemaphoreHandle_t slotMutex = nullptr;
Slot slot;
bool activityAttached = false;

struct SlotLock {
  SlotLock() {
    if (slotMutex) {
      xSemaphoreTake(slotMutex, portMAX_DELAY);
    }
  }
  ~SlotLock() {
    if (slotMutex) {
      xSemaphoreGive(slotMutex);
    }
  }
  SlotLock(const SlotLock&) = delete;
  SlotLock& operator=(const SlotLock&) = delete;
};

void ensureMutex() {
  if (!slotMutex) {
    slotMutex = xSemaphoreCreateMutex();
  }
}

uint32_t requestIdWord() {
#ifndef SIMULATOR
  return esp_random();
#else
  static uint32_t counter = 0;
  return millis() ^ (0x9e3779b9UL * ++counter);
#endif
}

void expireSlotLocked() {
  if (slot.state != SlotState::Empty && millis() - slot.createdAtMs >= kRequestTtlMs) {
    LOG_WRN("CLAUDE", "Question %s expired", slot.request.id.c_str());
    slot = Slot{};
  }
}

bool hasConfiguredToken() { return !CLAUDE_BRIDGE_STORE.token().empty(); }

bool authorized(WebServer* server) {
  const std::string& token = CLAUDE_BRIDGE_STORE.token();
  return !token.empty() && server->header("Authorization") == String(("Bearer " + token).c_str());
}

void sendJson(WebServer* server, const int code, const JsonDocument& doc) {
  std::string out;
  serializeJson(doc, out);
  server->send(code, "application/json", out.c_str());
}

void sendError(WebServer* server, const int code, const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  sendJson(server, code, doc);
}

std::string boundedString(const JsonVariantConst value, const char* fallback, const size_t maxChars) {
  std::string result = value.is<const char*>() ? value.as<const char*>() : fallback;
  if (result.size() > maxChars) {
    result.resize(maxChars);
  }
  return result;
}

bool parseQuestions(const JsonDocument& in, PendingRequest& request, const char*& error) {
  const JsonArrayConst questions = in["questions"].as<JsonArrayConst>();
  if (questions.isNull() || questions.size() == 0 || questions.size() > kMaxQuestions) {
    error = "questions must contain 1 to 4 items";
    return false;
  }

  request.questions.reserve(questions.size());
  for (const JsonVariantConst questionValue : questions) {
    const JsonObjectConst questionJson = questionValue.as<JsonObjectConst>();
    if (questionJson.isNull() || (questionJson["multiSelect"] | false)) {
      error = "multi-select questions are not supported on this device";
      return false;
    }

    ClaudeQuestion question;
    question.text = boundedString(questionJson["question"], "", kMaxQuestionChars);
    question.header = boundedString(questionJson["header"], "Claude", kMaxHeaderChars);
    if (question.text.empty() ||
        std::any_of(request.questions.begin(), request.questions.end(),
                    [&question](const ClaudeQuestion& existing) { return existing.text == question.text; })) {
      error = "question text must be present and unique";
      return false;
    }

    const JsonArrayConst options = questionJson["options"].as<JsonArrayConst>();
    if (options.isNull() || options.size() < 2 || options.size() > kMaxOptions) {
      error = "each question must contain 2 to 4 options";
      return false;
    }

    question.options.reserve(options.size());
    for (const JsonVariantConst optionValue : options) {
      const JsonObjectConst optionJson = optionValue.as<JsonObjectConst>();
      QuestionOption option;
      option.label = boundedString(optionJson["label"], "", kMaxOptionLabelChars);
      option.description = boundedString(optionJson["description"], "", kMaxOptionDescriptionChars);
      if (option.label.empty()) {
        error = "option labels must not be empty";
        return false;
      }
      question.options.push_back(std::move(option));
    }
    request.questions.push_back(std::move(question));
  }
  return true;
}

void handleAsk(WebServer* server) {
  if (!hasConfiguredToken()) {
    sendError(server, 503, "Claude control is not configured");
    return;
  }
  if (!authorized(server)) {
    sendError(server, 401, "unauthorized");
    return;
  }
  if (!server->hasArg("plain")) {
    sendError(server, 400, "missing body");
    return;
  }

  JsonDocument in;
  if (deserializeJson(in, server->arg("plain"))) {
    sendError(server, 400, "invalid json");
    return;
  }

  PendingRequest request;
  request.sessionTitle = boundedString(in["session"]["title"], "Claude Code", kMaxSessionTitleChars);
  request.cwd = boundedString(in["session"]["cwd"], "", kMaxCwdChars);
  const char* parseError = nullptr;
  if (!parseQuestions(in, request, parseError)) {
    sendError(server, 422, parseError);
    return;
  }

  char idBuf[24];
  snprintf(idBuf, sizeof(idBuf), "q%08lx%08lx", static_cast<unsigned long>(requestIdWord()),
           static_cast<unsigned long>(requestIdWord()));
  request.id = idBuf;

  {
    SlotLock lock;
    expireSlotLocked();
    if (!activityAttached) {
      sendError(server, 503, "Claude screen not open");
      return;
    }
    if (slot.state != SlotState::Empty) {
      sendError(server, 503, "another question is already on screen");
      return;
    }
    slot.request = std::move(request);
    slot.state = SlotState::Pending;
    slot.createdAtMs = millis();
  }

  JsonDocument out;
  out["id"] = idBuf;
  sendJson(server, 200, out);
  LOG_INF("CLAUDE", "Question %s queued", idBuf);
}

void handleAnswer(WebServer* server) {
  if (!authorized(server)) {
    sendError(server, 401, "unauthorized");
    return;
  }
  const String id = server->arg("id");

  JsonDocument out;
  {
    SlotLock lock;
    expireSlotLocked();
    if (slot.request.id != id.c_str()) {
      sendError(server, 409, "unknown request id");
      return;
    }
    if (slot.state == SlotState::Pending) {
      server->send(204, "application/json", "");
      return;
    }

    JsonObject answers = out["answers"].to<JsonObject>();
    for (const auto& answer : slot.answers) {
      answers[answer.question] = answer.value;
    }
    slot = Slot{};
  }
  sendJson(server, 200, out);
}

void handleCancel(WebServer* server) {
  if (!authorized(server)) {
    sendError(server, 401, "unauthorized");
    return;
  }
  cancelPending(server->arg("id").c_str());
  server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleStatus(WebServer* server) {
  SlotLock lock;
  expireSlotLocked();
  JsonDocument doc;
  doc["configured"] = hasConfiguredToken();
  doc["screen_open"] = activityAttached;
  doc["busy"] = slot.state != SlotState::Empty;
  sendJson(server, 200, doc);
}

bool shouldRegisterRoutes() { return core::FeatureCatalog::isEnabled("claude_bridge"); }

bool shouldExposeHomeAction(core::HomeActionEntry::HomeActionContext) {
  return core::FeatureCatalog::isEnabled("claude_bridge");
}

Activity* createHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, void* callbackCtx,
                             void (*onBack)(void* ctx)) {
  return new ClaudeBridgeActivity(renderer, mappedInput, [callbackCtx, onBack] {
    if (onBack != nullptr) {
      onBack(callbackCtx);
    }
  });
}

void mountRoutes(WebServer* server) {
  server->on("/api/claude/ask", HTTP_POST, [server] { handleAsk(server); });
  server->on("/api/claude/answer", HTTP_GET, [server] { handleAnswer(server); });
  server->on("/api/claude/cancel", HTTP_POST, [server] { handleCancel(server); });
  server->on("/api/claude/status", HTTP_GET, [server] { handleStatus(server); });
}

void onStorageReady() {
  ensureMutex();
  CLAUDE_BRIDGE_STORE.load();
}

}  // namespace
#endif

void registerFeature() {
#if ENABLE_CLAUDE_BRIDGE
  if (!core::FeatureModules::hasCapability(core::Capability::ClaudeBridge)) {
    return;
  }

  core::LifecycleEntry entry{};
  entry.onStorageReady = onStorageReady;
  core::LifecycleRegistry::add(entry);

  core::WebRouteEntry webRouteEntry{};
  webRouteEntry.routeId = "claude_bridge";
  webRouteEntry.shouldRegister = shouldRegisterRoutes;
  webRouteEntry.mountRoutes = mountRoutes;
  core::WebRouteRegistry::add(webRouteEntry);

  core::HomeActionEntry homeEntry{};
  homeEntry.actionId = "claude_bridge";
  homeEntry.shouldExpose = shouldExposeHomeAction;
  homeEntry.create = createHomeActivity;
  core::HomeActionRegistry::add(homeEntry);
#endif
}

#if ENABLE_CLAUDE_BRIDGE

void attachActivity() {
  ensureMutex();
  SlotLock lock;
  activityAttached = true;
}

void detachActivity() {
  SlotLock lock;
  activityAttached = false;
  if (slot.state == SlotState::Pending) {
    slot = Slot{};
  }
}

bool isConfigured() { return hasConfiguredToken(); }

bool peekPending(PendingRequest& out) {
  SlotLock lock;
  expireSlotLocked();
  if (slot.state != SlotState::Pending) {
    return false;
  }
  out = slot.request;
  return true;
}

bool isPending(const std::string& id) {
  SlotLock lock;
  expireSlotLocked();
  return slot.state == SlotState::Pending && slot.request.id == id;
}

bool submitAnswers(const std::string& id, const std::vector<QuestionAnswer>& answers) {
  SlotLock lock;
  expireSlotLocked();
  if (slot.state != SlotState::Pending || slot.request.id != id || answers.size() != slot.request.questions.size()) {
    return false;
  }

  for (size_t i = 0; i < answers.size(); ++i) {
    const ClaudeQuestion& question = slot.request.questions[i];
    if (answers[i].question != question.text ||
        std::none_of(question.options.begin(), question.options.end(),
                     [&answers, i](const QuestionOption& option) { return option.label == answers[i].value; })) {
      return false;
    }
  }

  slot.answers = answers;
  slot.state = SlotState::Answered;
  LOG_INF("CLAUDE", "Question %s answered", id.c_str());
  return true;
}

bool cancelPending(const std::string& id) {
  SlotLock lock;
  expireSlotLocked();
  if (slot.request.id != id) {
    return false;
  }
  slot = Slot{};
  return true;
}

#else

void attachActivity() {}
void detachActivity() {}
bool isConfigured() { return false; }
bool peekPending(PendingRequest&) { return false; }
bool isPending(const std::string&) { return false; }
bool submitAnswers(const std::string&, const std::vector<QuestionAnswer>&) { return false; }
bool cancelPending(const std::string&) { return false; }

#endif

}  // namespace features::claude_bridge
