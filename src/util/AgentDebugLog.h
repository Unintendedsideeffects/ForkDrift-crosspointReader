#pragma once

#include <Logging.h>

inline void agentDebugLog(const char* runId, const char* hypothesisId, const char* location, const char* message,
                          const char* data) {
  LOG_DBG("AGENT", "run=%s hyp=%s loc=%s msg=%s data=%s", runId, hypothesisId, location, message, data);
}
