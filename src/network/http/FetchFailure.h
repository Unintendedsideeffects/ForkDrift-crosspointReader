#pragma once

#include <cstddef>
#include <cstdint>

namespace http_fetch {

enum class Reason : uint8_t {
  Ok,
  LowMemory,
  AllocationFailure,
  IncompleteBody,
  InvalidXml,
  Authentication,
  TlsHandshake,
  Connect,
  HttpStatus,
  Aborted,
  Unknown,
};

struct Result {
  Reason reason = Reason::Unknown;
  int httpStatus = 0;
  bool ok() const { return reason == Reason::Ok; }
};

struct ClassifyInput {
  bool https = false;
  bool admitted = true;
  bool clientAllocated = true;
  int transportErr = 0;
  int httpStatus = 0;
  bool writeOk = true;
  bool aborted = false;
  bool overflowed = false;
  size_t expectedBody = 0;
  size_t receivedBody = 0;
  uint32_t freeHeap = 0;
  uint32_t minHeapForTls = 38000;
};

constexpr int kEspOk = 0;
constexpr int kEspFail = -1;
constexpr int kEspErrNoMem = 0x101;
constexpr int kEspErrHttpMaxRedirect = 0x7001;
constexpr int kEspErrHttpConnect = 0x7002;
constexpr int kEspErrHttpConnecting = 0x7006;
constexpr int kEspErrHttpIncompleteData = 0x700C;

constexpr const char* reasonName(const Reason reason) {
  switch (reason) {
    case Reason::Ok:
      return "ok";
    case Reason::LowMemory:
      return "low memory";
    case Reason::AllocationFailure:
      return "allocation failure";
    case Reason::IncompleteBody:
      return "incomplete body";
    case Reason::InvalidXml:
      return "invalid XML";
    case Reason::Authentication:
      return "authentication";
    case Reason::TlsHandshake:
      return "tls handshake";
    case Reason::Connect:
      return "connect";
    case Reason::HttpStatus:
      return "http status";
    case Reason::Aborted:
      return "aborted";
    case Reason::Unknown:
      return "unknown";
  }
  return "unknown";
}

constexpr bool logAsError(const Reason reason) {
  switch (reason) {
    case Reason::Ok:
    case Reason::LowMemory:
    case Reason::AllocationFailure:
    case Reason::Authentication:
    case Reason::Aborted:
      return false;
    case Reason::IncompleteBody:
    case Reason::InvalidXml:
    case Reason::TlsHandshake:
    case Reason::Connect:
    case Reason::HttpStatus:
    case Reason::Unknown:
      return true;
  }
  return true;
}

constexpr Reason classify(const ClassifyInput& input) {
  if (!input.admitted) {
    return Reason::LowMemory;
  }
  if (!input.clientAllocated) {
    return Reason::AllocationFailure;
  }
  if (input.aborted) {
    return Reason::Aborted;
  }
  if (input.overflowed) {
    return Reason::IncompleteBody;
  }
  if (input.httpStatus == 401 || input.httpStatus == 403) {
    return Reason::Authentication;
  }
  if (input.transportErr == kEspErrNoMem) {
    return Reason::AllocationFailure;
  }
  if (input.transportErr != kEspOk) {
    if (input.https && input.freeHeap < input.minHeapForTls) {
      return Reason::LowMemory;
    }
    if (input.transportErr == kEspErrHttpIncompleteData) {
      return Reason::IncompleteBody;
    }
    if (input.transportErr == kEspErrHttpConnect || input.transportErr == kEspErrHttpConnecting) {
      return Reason::Connect;
    }
    if (input.https) {
      return Reason::TlsHandshake;
    }
    return Reason::Connect;
  }
  if (input.httpStatus != 0 && input.httpStatus != 200) {
    return Reason::HttpStatus;
  }
  if (!input.writeOk) {
    return Reason::IncompleteBody;
  }
  if (input.expectedBody > 0 && input.receivedBody < input.expectedBody) {
    return Reason::IncompleteBody;
  }
  return Reason::Ok;
}

constexpr Reason classifyParse(const bool parserError, const bool truncated, const size_t entryCount) {
  if (parserError) {
    return Reason::InvalidXml;
  }
  if (truncated && entryCount == 0) {
    return Reason::IncompleteBody;
  }
  return Reason::Ok;
}

}  // namespace http_fetch
