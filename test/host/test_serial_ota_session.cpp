#include <cstring>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "network/ota/SerialOtaSession.h"

namespace {

struct ReplyCapture {
  std::vector<std::string> lines;
};

void captureReply(void* ctx, const char* line) { static_cast<ReplyCapture*>(ctx)->lines.push_back(line); }

struct FakeFlashCtx : OtaFlashOpsState {
  std::vector<std::string> callLog;
  bool beginOk = true;
  bool writeOk = true;
  bool endOk = true;
  const char* beginFailReason = "fake_begin";
  const char* writeFailReason = "fake_write";
  const char* endFailReason = "fake_end";
};

bool fakeBegin(void* ctx) {
  auto* fake = static_cast<FakeFlashCtx*>(ctx);
  fake->callLog.push_back("begin");
  if (!fake->beginOk) {
    strncpy(fake->lastError, fake->beginFailReason, sizeof(fake->lastError));
    fake->lastError[sizeof(fake->lastError) - 1] = '\0';
    return false;
  }
  return true;
}

bool fakeWrite(void* ctx, const uint8_t* data, const size_t len) {
  auto* fake = static_cast<FakeFlashCtx*>(ctx);
  fake->callLog.push_back("write:" + std::to_string(len));
  (void)data;
  if (!fake->writeOk) {
    strncpy(fake->lastError, fake->writeFailReason, sizeof(fake->lastError));
    fake->lastError[sizeof(fake->lastError) - 1] = '\0';
    return false;
  }
  return true;
}

bool fakeEnd(void* ctx) {
  auto* fake = static_cast<FakeFlashCtx*>(ctx);
  fake->callLog.push_back("end");
  if (!fake->endOk) {
    strncpy(fake->lastError, fake->endFailReason, sizeof(fake->lastError));
    fake->lastError[sizeof(fake->lastError) - 1] = '\0';
    return false;
  }
  return true;
}

void fakeAbort(void* ctx) {
  auto* fake = static_cast<FakeFlashCtx*>(ctx);
  fake->callLog.push_back("abort");
}

OtaFlashOps makeFakeOps(FakeFlashCtx& ctx) {
  return OtaFlashOps{
      .begin = fakeBegin,
      .write = fakeWrite,
      .end = fakeEnd,
      .abort = fakeAbort,
      .ctx = &ctx,
  };
}

std::string base64Encode(const uint8_t* data, const size_t len) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t block = (static_cast<uint32_t>(data[i]) << 16) |
                           ((i + 1 < len) ? static_cast<uint32_t>(data[i + 1]) << 8 : 0) |
                           ((i + 2 < len) ? static_cast<uint32_t>(data[i + 2]) : 0);
    out.push_back(alphabet[(block >> 18) & 0x3F]);
    out.push_back(alphabet[(block >> 12) & 0x3F]);
    out.push_back((i + 1 < len) ? alphabet[(block >> 6) & 0x3F] : '=');
    out.push_back((i + 2 < len) ? alphabet[block & 0x3F] : '=');
  }
  return out;
}

String makeOtaDataCmd(const std::string& b64) { return String("OTA_DATA:") + b64.c_str(); }

SerialOtaHandleResult runCommand(SerialOtaSession& session, const String& cmd, FakeFlashCtx& fake,
                                 ReplyCapture& replies, const unsigned long nowMs) {
  return session.handleCommand(cmd, makeFakeOps(fake), nowMs, captureReply, &replies);
}

}  // namespace

TEST_CASE("serial OTA happy path: BEGIN, DATA, END") {
  SerialOtaSession session;
  FakeFlashCtx fake;
  ReplyCapture replies;
  unsigned long now = 1000;

  auto begin = runCommand(session, "OTA_BEGIN", fake, replies, now);
  CHECK(begin.handled);
  CHECK_FALSE(begin.restartRequested);
  CHECK(replies.lines.back() == "OTA_OK\n");
  CHECK(session.inProgress());

  const uint8_t chunk1[] = {1, 2, 3, 4};
  const uint8_t chunk2[] = {5, 6, 7};
  now += 100;
  auto data1 = runCommand(session, makeOtaDataCmd(base64Encode(chunk1, sizeof(chunk1))), fake, replies, now);
  CHECK(data1.handled);
  CHECK(replies.lines.back() == "OTA_OK\n");

  now += 100;
  auto data2 = runCommand(session, makeOtaDataCmd(base64Encode(chunk2, sizeof(chunk2))), fake, replies, now);
  CHECK(data2.handled);
  CHECK(replies.lines.back() == "OTA_OK\n");

  now += 100;
  auto end = runCommand(session, "OTA_END", fake, replies, now);
  CHECK(end.handled);
  CHECK(end.restartRequested);
  CHECK(replies.lines.back() == "OTA_OK\n");
  CHECK_FALSE(session.inProgress());

  REQUIRE(fake.callLog.size() == 4);
  CHECK(fake.callLog[0] == "begin");
  CHECK(fake.callLog[1] == "write:4");
  CHECK(fake.callLog[2] == "write:3");
  CHECK(fake.callLog[3] == "end");
}

TEST_CASE("serial OTA DATA before BEGIN returns not started") {
  SerialOtaSession session;
  FakeFlashCtx fake;
  ReplyCapture replies;

  const uint8_t chunk[] = {9};
  const auto result = runCommand(session, makeOtaDataCmd(base64Encode(chunk, sizeof(chunk))), fake, replies, 1000);
  CHECK(result.handled);
  CHECK_FALSE(result.restartRequested);
  CHECK(replies.lines.back() == "OTA_ERR:not started\n");
  CHECK(fake.callLog.empty());
  CHECK_FALSE(session.inProgress());
}

TEST_CASE("serial OTA oversized chunk aborts with base64 error") {
  SerialOtaSession session;
  FakeFlashCtx fake;
  ReplyCapture replies;

  CHECK(runCommand(session, "OTA_BEGIN", fake, replies, 1000).handled);

  std::vector<uint8_t> oversized(1537, 0xAB);
  const auto result =
      runCommand(session, makeOtaDataCmd(base64Encode(oversized.data(), oversized.size())), fake, replies, 1100);
  CHECK(result.handled);
  CHECK(replies.lines.back() == "OTA_ERR:base64\n");
  CHECK(fake.callLog.back() == "abort");
  CHECK_FALSE(session.inProgress());
}

TEST_CASE("serial OTA invalid base64 aborts with base64 error") {
  SerialOtaSession session;
  FakeFlashCtx fake;
  ReplyCapture replies;

  CHECK(runCommand(session, "OTA_BEGIN", fake, replies, 1000).handled);

  const auto result = runCommand(session, "OTA_DATA:!!!not-base64!!!", fake, replies, 1100);
  CHECK(result.handled);
  CHECK(replies.lines.back() == "OTA_ERR:base64\n");
  CHECK(fake.callLog.back() == "abort");
  CHECK_FALSE(session.inProgress());
}

TEST_CASE("serial OTA write failure aborts and unlatches") {
  SerialOtaSession session;
  FakeFlashCtx fake;
  ReplyCapture replies;
  fake.writeOk = false;
  fake.writeFailReason = "write_fail";

  CHECK(runCommand(session, "OTA_BEGIN", fake, replies, 1000).handled);

  const uint8_t chunk[] = {1};
  const auto result = runCommand(session, makeOtaDataCmd(base64Encode(chunk, sizeof(chunk))), fake, replies, 1100);
  CHECK(result.handled);
  CHECK(replies.lines.back() == "OTA_ERR:write_fail\n");
  CHECK(fake.callLog.back() == "abort");
  CHECK_FALSE(session.inProgress());
}

TEST_CASE("serial OTA ABORT mid-stream aborts and unlatches") {
  SerialOtaSession session;
  FakeFlashCtx fake;
  ReplyCapture replies;

  CHECK(runCommand(session, "OTA_BEGIN", fake, replies, 1000).handled);

  const auto result = runCommand(session, "OTA_ABORT", fake, replies, 1100);
  CHECK(result.handled);
  CHECK(replies.lines.back() == "OTA_OK\n");
  CHECK(fake.callLog.back() == "abort");
  CHECK_FALSE(session.inProgress());
}

TEST_CASE("serial OTA BEGIN twice aborts prior session") {
  SerialOtaSession session;
  FakeFlashCtx fake;
  ReplyCapture replies;

  CHECK(runCommand(session, "OTA_BEGIN", fake, replies, 1000).handled);
  CHECK(session.inProgress());

  const auto result = runCommand(session, "OTA_BEGIN", fake, replies, 1100);
  CHECK(result.handled);
  CHECK(replies.lines.back() == "OTA_OK\n");
  CHECK(session.inProgress());

  REQUIRE(fake.callLog.size() == 3);
  CHECK(fake.callLog[0] == "begin");
  CHECK(fake.callLog[1] == "abort");
  CHECK(fake.callLog[2] == "begin");
}

TEST_CASE("serial OTA idle timeout aborts and unlatches") {
  SerialOtaSession session;
  FakeFlashCtx fake;
  ReplyCapture replies;
  const OtaFlashOps ops = makeFakeOps(fake);

  CHECK(runCommand(session, "OTA_BEGIN", fake, replies, 1000).handled);
  CHECK(session.inProgress());

  const unsigned long timedOutAt = 1000 + SerialOtaSession::kIdleTimeoutMs + 1;
  CHECK(session.checkIdleTimeout(ops, timedOutAt));
  CHECK(fake.callLog.back() == "abort");
  CHECK_FALSE(session.inProgress());
}

TEST_CASE("serial OTA non-command returns false with no state change") {
  SerialOtaSession session;
  FakeFlashCtx fake;
  ReplyCapture replies;

  const auto result = runCommand(session, "CMD:NOT_OTA", fake, replies, 1000);
  CHECK_FALSE(result.handled);
  CHECK_FALSE(result.restartRequested);
  CHECK(replies.lines.empty());
  CHECK(fake.callLog.empty());
  CHECK_FALSE(session.inProgress());
}
