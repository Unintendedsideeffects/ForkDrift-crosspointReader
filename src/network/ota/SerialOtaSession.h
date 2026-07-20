#pragma once

#include <WString.h>

#include <cstddef>
#include <cstdint>

struct OtaFlashOpsState {
  char lastError[48];
};

struct OtaFlashOps {
  bool (*begin)(void* ctx);
  bool (*write)(void* ctx, const uint8_t* data, size_t len);
  bool (*end)(void* ctx);
  void (*abort)(void* ctx);
  void* ctx;
};

inline OtaFlashOpsState* otaFlashOpsState(void* ctx) { return static_cast<OtaFlashOpsState*>(ctx); }

struct SerialOtaHandleResult {
  bool handled;
  bool restartRequested;
};

using SerialOtaReplyFn = void (*)(void* ctx, const char* line);

class SerialOtaSession {
 public:
  static constexpr unsigned long kIdleTimeoutMs = 30000;

  bool inProgress() const { return inProgress_; }
  unsigned long lastActivityMs() const { return lastActivityMs_; }

  SerialOtaHandleResult handleCommand(const String& cmd, const OtaFlashOps& ops, unsigned long nowMs,
                                      SerialOtaReplyFn emitReply, void* replyCtx);

  bool checkIdleTimeout(const OtaFlashOps& ops, unsigned long nowMs);

 private:
  void abortSession(const OtaFlashOps& ops);
  void emitError(SerialOtaReplyFn emitReply, void* replyCtx, const char* reason);

  bool inProgress_ = false;
  unsigned long lastActivityMs_ = 0;
  uint8_t decodeBuffer_[1536] = {};
};
