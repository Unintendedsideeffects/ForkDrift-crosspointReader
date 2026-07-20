#include "network/ota/SerialOtaSession.h"

#include <mbedtls/base64.h>

#include <cstdio>
#include <cstring>

void SerialOtaSession::abortSession(const OtaFlashOps& ops) {
  if (!inProgress_) {
    return;
  }
  ops.abort(ops.ctx);
  inProgress_ = false;
}

void SerialOtaSession::emitError(SerialOtaReplyFn emitReply, void* replyCtx, const char* reason) {
  char line[64];
  snprintf(line, sizeof(line), "OTA_ERR:%s\n", reason);
  emitReply(replyCtx, line);
}

SerialOtaHandleResult SerialOtaSession::handleCommand(const String& cmd, const OtaFlashOps& ops,
                                                      const unsigned long nowMs, SerialOtaReplyFn emitReply,
                                                      void* replyCtx) {
  if (cmd == "OTA_BEGIN") {
    lastActivityMs_ = nowMs;
    abortSession(ops);
    if (!ops.begin(ops.ctx)) {
      emitError(emitReply, replyCtx, otaFlashOpsState(ops.ctx)->lastError);
      return {true, false};
    }
    inProgress_ = true;
    emitReply(replyCtx, "OTA_OK\n");
    return {true, false};
  }

  if (cmd.startsWith("OTA_DATA:")) {
    lastActivityMs_ = nowMs;
    if (!inProgress_) {
      emitError(emitReply, replyCtx, "not started");
      return {true, false};
    }
    const char* b64 = cmd.c_str() + 9;
    size_t outLen = 0;
    const int rc = mbedtls_base64_decode(decodeBuffer_, sizeof(decodeBuffer_), &outLen,
                                         reinterpret_cast<const uint8_t*>(b64), strlen(b64));
    if (rc != 0) {
      abortSession(ops);
      emitError(emitReply, replyCtx, "base64");
      return {true, false};
    }
    if (!ops.write(ops.ctx, decodeBuffer_, outLen)) {
      abortSession(ops);
      emitError(emitReply, replyCtx, otaFlashOpsState(ops.ctx)->lastError);
      return {true, false};
    }
    emitReply(replyCtx, "OTA_OK\n");
    return {true, false};
  }

  if (cmd == "OTA_END") {
    lastActivityMs_ = nowMs;
    if (!inProgress_) {
      emitError(emitReply, replyCtx, "not started");
      return {true, false};
    }
    inProgress_ = false;
    if (!ops.end(ops.ctx)) {
      emitError(emitReply, replyCtx, otaFlashOpsState(ops.ctx)->lastError);
      return {true, false};
    }
    emitReply(replyCtx, "OTA_OK\n");
    return {true, true};
  }

  if (cmd == "OTA_ABORT") {
    lastActivityMs_ = nowMs;
    abortSession(ops);
    emitReply(replyCtx, "OTA_OK\n");
    return {true, false};
  }

  return {false, false};
}

bool SerialOtaSession::checkIdleTimeout(const OtaFlashOps& ops, const unsigned long nowMs) {
  if (!inProgress_) {
    return false;
  }
  if (nowMs - lastActivityMs_ <= kIdleTimeoutMs) {
    return false;
  }
  abortSession(ops);
  return true;
}
