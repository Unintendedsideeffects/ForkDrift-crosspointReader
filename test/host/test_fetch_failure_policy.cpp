#include <string>

#include "doctest/doctest.h"
#include "network/http/FetchFailure.h"

TEST_CASE("401 after redirect is authentication, not a protocol error") {
  using namespace http_fetch;

  const Reason reason = classify({
      .https = true,
      .admitted = true,
      .clientAllocated = true,
      .transportErr = kEspErrHttpMaxRedirect,
      .httpStatus = 401,
      .freeHeap = 65000,
      .minHeapForTls = 38000,
  });
  CHECK(reason == Reason::Authentication);
  CHECK(reasonName(reason) == std::string("authentication"));
  CHECK_FALSE(logAsError(reason));
}

TEST_CASE("heap below the TLS floor is low memory, not connect") {
  using namespace http_fetch;

  CHECK(classify({.https = true, .admitted = false, .freeHeap = 12000, .minHeapForTls = 38000}) == Reason::LowMemory);
  CHECK(classify({
            .https = true,
            .admitted = true,
            .transportErr = kEspErrHttpConnect,
            .freeHeap = 8000,
            .minHeapForTls = 38000,
        }) == Reason::LowMemory);
  CHECK_FALSE(logAsError(Reason::LowMemory));
  CHECK_FALSE(logAsError(Reason::AllocationFailure));
}

TEST_CASE("connect at healthy heap stays connect") {
  using namespace http_fetch;

  CHECK(classify({
            .https = true,
            .transportErr = kEspErrHttpConnect,
            .freeHeap = 64000,
            .minHeapForTls = 38000,
        }) == Reason::Connect);
  CHECK(logAsError(Reason::Connect));
}

TEST_CASE("incomplete body and invalid XML are distinct from fetch protocol errors") {
  using namespace http_fetch;

  CHECK(classify({.transportErr = kEspErrHttpIncompleteData, .httpStatus = 200}) == Reason::IncompleteBody);
  CHECK(classify({.httpStatus = 200, .writeOk = false}) == Reason::IncompleteBody);
  CHECK(classify({.httpStatus = 200, .overflowed = true}) == Reason::IncompleteBody);
  CHECK(classify({.httpStatus = 200, .expectedBody = 100, .receivedBody = 40}) == Reason::IncompleteBody);
  CHECK(classifyParse(true, false, 0) == Reason::InvalidXml);
  CHECK(classifyParse(false, true, 0) == Reason::IncompleteBody);
  CHECK(classifyParse(false, true, 3) == Reason::Ok);
  CHECK(classifyParse(false, false, 0) == Reason::Ok);
}

TEST_CASE("client init failure is allocation, not connect") {
  using namespace http_fetch;

  CHECK(classify({.clientAllocated = false, .freeHeap = 40000}) == Reason::AllocationFailure);
  CHECK(classify({.transportErr = kEspErrNoMem, .httpStatus = 0}) == Reason::AllocationFailure);
}
