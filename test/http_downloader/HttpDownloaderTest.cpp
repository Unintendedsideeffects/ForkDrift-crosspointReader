#include <gtest/gtest.h>

#include "HttpDownloadFilename.h"
#include "HttpDownloadResume.h"
#include "HttpDownloader.h"
#include "TestPlatform.h"

class HttpDownloaderTest : public testing::TestWithParam<HttpDownloader::Transport> {
  void SetUp() override {
    files.clear();
    replies.clear();
    requests.clear();
    requestUrls.clear();
    nextReply = 0;
    failWrite = failSync = false;
  }

 protected:
  std::string resolved;
  HttpDownloader::DownloadError serverDownload(const std::string& approved = "",
                                               HttpDownloader::CancelCallback cancel = nullptr,
                                               bool (*validate)(const std::string&) = nullptr) {
    HttpDownloader::DownloadOptions options;
    options.transport = GetParam();
    options.stageAsPart = true;
    options.useServerFilename = true;
    options.overwriteApprovedPath = approved;
    options.resolvedPath = &resolved;
    options.shouldCancel = std::move(cancel);
    options.validate = validate;
    return HttpDownloader::downloadToFile("https://example.com/book", "/Books/Author - Title.epub", nullptr, nullptr,
                                          "user", "pass", options);
  }
  static Reply named(std::string name = "Server_Name.epub", std::string body = "abc", int status = 200,
                     size_t length = 3, bool complete = true, std::string range = "") {
    Reply reply{status, std::move(body), length, std::move(range), complete};
    reply.disposition = "attachment; filename=\"" + name + "\"";
    return reply;
  }
  HttpDownloader::DownloadError download(HttpDownloader::CancelCallback cancel = nullptr) {
    HttpDownloader::DownloadOptions options;
    options.transport = GetParam();
    options.stageAsPart = true;
    options.shouldCancel = cancel;
    return HttpDownloader::downloadToFile("https://example.com/book", "/book", nullptr, nullptr, "user", "pass",
                                          options);
  }
};
TEST_P(HttpDownloaderTest, ReconnectsAtLastWrittenByte) {
  replies = {{200, "abc", 6, "", false}, {206, "def", 3, "bytes 3-5/6", true}};
  ASSERT_EQ(download(), HttpDownloader::OK);
  EXPECT_EQ(files["/book"], "abcdef");
  EXPECT_EQ(requests[1]["Range"], "bytes=3-");
}
TEST_P(HttpDownloaderTest, IgnoredRangeRestartsWithoutDuplicateBytes) {
  replies = {{200, "abc", 6, "", false}, {200, "abcdef", 6}, {200, "abcdef", 6}};
  ASSERT_EQ(download(), HttpDownloader::OK);
  EXPECT_EQ(files["/book"], "abcdef");
  EXPECT_EQ(requests.size(), 3u);
  EXPECT_EQ(requests[2].count("Range"), 0u);
}
TEST_P(HttpDownloaderTest, RejectsWrongOffsetsAndChangedSizes) {
  for (const auto* range : {"bytes 2-4/6", "bytes 3-5/7", "bytes 3-9/6", "bytes 3-5/*"}) {
    replies = {{200, "abc", 6, "", false}, {206, "def", 3, range, true}};
    nextReply = 0;
    requests.clear();
    files.clear();
    files["/book"] = "old";
    EXPECT_EQ(download(), HttpDownloader::HTTP_ERROR);
    EXPECT_EQ(files["/book"], "old");
    EXPECT_FALSE(files.count("/book.part"));
  }
}
TEST_P(HttpDownloaderTest, PartialRangeRequiresAnotherRequest) {
  replies = {{200, "ab", 6, "", false}, {206, "cd", 2, "bytes 2-3/6", true}, {206, "ef", 2, "bytes 4-5/6", true}};
  EXPECT_EQ(download(), HttpDownloader::OK);
  EXPECT_EQ(files["/book"], "abcdef");
}
TEST_P(HttpDownloaderTest, EmptyIgnoredRangeStillRestarts) {
  replies = {{200, "ab", 6, "", false}, {200, "", 6}, {200, "abcdef", 6}};
  EXPECT_EQ(download(), HttpDownloader::OK);
  EXPECT_EQ(files["/book"], "abcdef");
}
TEST_P(HttpDownloaderTest, StopsAfterThreeStalls) {
  replies = {{200, "", 6, "", false}, {200, "", 6, "", false}, {200, "", 6, "", false}, {200, "abcdef", 6}};
  EXPECT_EQ(download(), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(nextReply, 3u);
}
TEST_P(HttpDownloaderTest, CancellationAndSdFailureNeverRetry) {
  replies = {{200, "abc", 6, "", false}, {206, "def", 3, "bytes 3-5/6", true}};
  EXPECT_EQ(download([] { return true; }), HttpDownloader::ABORTED);
  EXPECT_EQ(nextReply, 0u);
  failWrite = true;
  EXPECT_EQ(download(), HttpDownloader::FILE_ERROR);
  EXPECT_EQ(nextReply, 1u);
}
TEST_P(HttpDownloaderTest, FlushFailureKeepsOriginal) {
  files["/book"] = "old";
  replies = {{200, "abcdef", 6}};
  failSync = true;
  EXPECT_EQ(download(), HttpDownloader::FILE_ERROR);
  EXPECT_EQ(files["/book"], "old");
}
TEST_P(HttpDownloaderTest, RejectsHttpsDowngradeAndOmitsCrossOriginCredentials) {
  replies = {{302, "", 0, "", true, "http://example.com/book"}};
  EXPECT_EQ(download(), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(nextReply, 1u);
  replies = {{302, "", 0, "", true, "https://other.example/book"}, {200, "abc", 3}};
  nextReply = 0;
  requests.clear();
  EXPECT_EQ(download(), HttpDownloader::OK);
  EXPECT_TRUE(requests[0].count("Authorization"));
  EXPECT_FALSE(requests[1].count("Authorization"));
}
TEST_P(HttpDownloaderTest, DoesNotRetryHttpErrorsOrOversizedBody) {
  replies = {{404, "missing", 7}};
  EXPECT_EQ(download(), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(nextReply, 1u);
  replies = {{200, "abcdef", 3}};
  nextReply = 0;
  EXPECT_EQ(download(), HttpDownloader::HTTP_ERROR);
}

TEST_P(HttpDownloaderTest, MissingValidatorRestartsFromZero) {
  replies = {{200, "abc", 6, "", false, "", ""}, {200, "UVWXYZ", 6, "", true, "", ""}};
  EXPECT_EQ(download(), HttpDownloader::OK);
  EXPECT_EQ(files["/book"], "UVWXYZ");
  ASSERT_EQ(requests.size(), 2u);
  EXPECT_EQ(requests[1].count("Range"), 0u);
}

TEST_P(HttpDownloaderTest, SendsIfRangeAndRejectsChangedRepresentation) {
  replies = {{200, "abc", 6, "", false, "", "\"v1\""}, {206, "XYZ", 3, "bytes 3-5/6", true, "", "\"v2\""}};
  EXPECT_EQ(download(), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(requests[1]["If-Range"], "\"v1\"");
  EXPECT_FALSE(files.count("/book"));
}
TEST_P(HttpDownloaderTest, CancelsBetweenRetryAttempts) {
  files["/book"] = "old";
  replies = {{200, "abc", 6, "", false}, {206, "def", 3, "bytes 3-5/6", true}};
  EXPECT_EQ(download([] { return files["/book.part"].size() == 3; }), HttpDownloader::ABORTED);
  EXPECT_EQ(nextReply, 1u);
  EXPECT_EQ(files["/book"], "old");
  EXPECT_FALSE(files.count("/book.part"));
}

TEST_P(HttpDownloaderTest, RetriesTransportFailureWithoutLosingPartialBytes) {
  replies = {{200, "abc", 6, "", false}, {-1, "", 0}, {206, "def", 3, "bytes 3-5/6", true}};
  EXPECT_EQ(download(), HttpDownloader::OK);
  EXPECT_EQ(files["/book"], "abcdef");
  EXPECT_EQ(requests[2]["Range"], "bytes=3-");
}

TEST_P(HttpDownloaderTest, ServerFilenameUsesFinalResponseAndKeepsGeneratedFile) {
  files["/Books/Author - Title.epub"] = "unrelated";
  Reply redirect = named("Wrong.epub", "", 302, 0);
  redirect.location = "https://other.example/download";
  replies = {redirect, named()};
  ASSERT_EQ(serverDownload(), HttpDownloader::OK);
  EXPECT_EQ(resolved, "/Books/Server_Name.epub");
  EXPECT_EQ(files[resolved], "abc");
  EXPECT_EQ(files["/Books/Author - Title.epub"], "unrelated");
  EXPECT_FALSE(files.count("/Books/Wrong.epub"));
  EXPECT_FALSE(requests[1].count("Authorization"));
}
TEST_P(HttpDownloaderTest, ServerNameFallbacksAndCollisionApproval) {
  for (const auto* header : {"", "attachment; filename=../escape.epub", "attachment; filename*=UTF-8''%00.epub"}) {
    files.clear();
    requests.clear();
    nextReply = 0;
    auto reply = named();
    reply.disposition = header;
    replies = {reply};
    EXPECT_EQ(serverDownload(), HttpDownloader::OK);
    EXPECT_EQ(resolved, "/Books/Author - Title.epub");
    EXPECT_EQ(files[resolved], "abc");
  }
  files.clear();
  nextReply = 0;
  replies = {named()};
  files["/Books/Server_Name.epub"] = "old";
  files["/Books/Server_Name.epub.part"] = "pending";
  EXPECT_EQ(serverDownload(), HttpDownloader::FILE_EXISTS);
  EXPECT_EQ(files[resolved], "old");
  EXPECT_EQ(files[resolved + ".part"], "pending");
  nextReply = 0;
  EXPECT_EQ(serverDownload("/Books/Other.epub"), HttpDownloader::FILE_EXISTS);
  nextReply = 0;
  EXPECT_EQ(serverDownload("/Books/Server_Name.epub"), HttpDownloader::OK);
  EXPECT_EQ(files[resolved], "abc");
  EXPECT_FALSE(files.count(resolved + ".part"));
  EXPECT_FALSE(files.count(resolved + ".old"));
}
TEST_P(HttpDownloaderTest, ServerNameResumesSameIdentityWhenHeaderIsOmitted) {
  auto first = named("Exact.epub", "abc", 200, 6, false);
  Reply second{206, "def", 3, "bytes 3-5/6"};
  replies = {first, second};
  EXPECT_EQ(serverDownload(), HttpDownloader::OK);
  EXPECT_EQ(resolved, "/Books/Exact.epub");
  EXPECT_EQ(files[resolved], "abcdef");
  EXPECT_EQ(requests[1]["Range"], "bytes=3-");
  EXPECT_EQ(requests[1]["If-Range"], "\"v1\"");
}
TEST_P(HttpDownloaderTest, ServerNameChangeDuringResumeNeverWritesAnotherBook) {
  replies = {named("Exact.epub", "abc", 200, 6, false), named("Other.epub", "def", 206, 3, true, "bytes 3-5/6")};
  EXPECT_EQ(serverDownload(), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(resolved, "/Books/Exact.epub");
  EXPECT_TRUE(files.empty());
}
TEST_P(HttpDownloaderTest, ApprovedReplacementPreservesOriginalOnSdOrValidationFailure) {
  files["/Books/Server_Name.epub"] = "old";
  replies = {named()};
  failWrite = true;
  EXPECT_EQ(serverDownload("/Books/Server_Name.epub"), HttpDownloader::FILE_ERROR);
  EXPECT_EQ(files["/Books/Server_Name.epub"], "old");
  failWrite = false;
  nextReply = 0;
  EXPECT_EQ(serverDownload("/Books/Server_Name.epub", nullptr, [](const std::string&) { return false; }),
            HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(files["/Books/Server_Name.epub"], "old");
  EXPECT_FALSE(files.count("/Books/Server_Name.epub.part"));
}
TEST_P(HttpDownloaderTest, ServerNameCancellationDeletesOnlyItsPartial) {
  files["/Books/Server_Name.epub"] = "old";
  replies = {named("Server_Name.epub", "abc", 200, 6, false)};
  EXPECT_EQ(serverDownload("/Books/Server_Name.epub",
                           [] {
                             return files.count("/Books/Server_Name.epub.part") &&
                                    files["/Books/Server_Name.epub.part"].size() == 3;
                           }),
            HttpDownloader::ABORTED);
  EXPECT_EQ(files["/Books/Server_Name.epub"], "old");
  EXPECT_FALSE(files.count("/Books/Server_Name.epub.part"));
}
TEST_P(HttpDownloaderTest, Utf8ExtendedNameAndExtensionCaseArePreserved) {
  auto reply = named();
  reply.disposition = "attachment; filename=Fallback.epub; filename*=UTF-8'en'%E6%97%A5%E6%9C%AC%20%F0%9F%93%9A.EPUB";
  replies = {reply};
  EXPECT_EQ(serverDownload(), HttpDownloader::OK);
  EXPECT_EQ(resolved, "/Books/日本 📚.EPUB");
  EXPECT_EQ(files[resolved], "abc");
}
TEST_P(HttpDownloaderTest, ServerErrorsAndEmptyBodiesDoNotReplaceOrRemoveExistingBooks) {
  files["/Books/Author - Title.epub"] = "fallback";
  files["/Books/Server_Name.epub"] = "old";
  replies = {named("Server_Name.epub", "error", 401, 5)};
  EXPECT_EQ(serverDownload(), HttpDownloader::HTTP_ERROR);
  nextReply = 0;
  replies = {named("Server_Name.epub", "", 200, 0)};
  EXPECT_EQ(serverDownload(), HttpDownloader::FILE_EXISTS);
  nextReply = 0;
  EXPECT_EQ(serverDownload("/Books/Server_Name.epub"), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(files["/Books/Server_Name.epub"], "old");
  EXPECT_EQ(files["/Books/Author - Title.epub"], "fallback");
}
TEST(HttpDownloadFilenameTest, QuotedSemicolonsAndUtf8AreExact) {
  std::string name;
  ASSERT_TRUE(HttpDownloadFilename::parse("attachment; FILENAME = \"The Guest List_ A Novel; Lucy.epub\"", name));
  EXPECT_EQ(name, "The Guest List_ A Novel; Lucy.epub");
  ASSERT_TRUE(HttpDownloadFilename::parse("inline; filename=Literal%20Space.epub", name));
  EXPECT_EQ(name, "Literal%20Space.epub");
  ASSERT_TRUE(HttpDownloadFilename::parse("attachment; filename*=utf-8''Cafe%CC%81.epub", name));
  EXPECT_EQ(name, "Café.epub");
  ASSERT_TRUE(HttpDownloadFilename::parse("attachment; filename=Safe.epub; filename*=UNKNOWN''bad", name));
  EXPECT_EQ(name, "Safe.epub");
}
TEST(HttpDownloadFilenameTest, RejectsUnsafeAmbiguousAndMalformedNames) {
  std::string name;
  for (const auto* header :
       {"attachment; filename=../escape.epub", "attachment; filename=folder\\escape.epub",
        "attachment; filename=.hidden.epub", "attachment; filename=Book:Title.epub", "attachment; filename=Book.epub.",
        "attachment; filename=Book.exe", "attachment; filename=\"unfinished.epub",
        "attachment; filename=a.epub; FILENAME=b.epub", "attachment; filename*=UTF-8''bad%GG.epub",
        "attachment; filename*=UTF-8''bad%00.epub", "attachment; filename*=UTF-8''%C0%AF.epub",
        "attachment; filename*=UTF-8''%ED%A0%80.epub", "attachment; filename*=UTF-8''%F4%90%80%80.epub",
        "attachment; filename*=UTF-8''%2Fescape.epub", "attachment; filename=bad\r\n.epub"}) {
    EXPECT_FALSE(HttpDownloadFilename::parse(header, name)) << header;
    EXPECT_TRUE(name.empty());
  }
}
TEST(HttpDownloadFilenameTest, RejectsDesktopReservedNames) {
  std::string name;
  for (const auto* value : {"CON.epub", "prn.epub", "NUL .epub", "COM1.epub", "Lpt9.epub"})
    EXPECT_FALSE(HttpDownloadFilename::parse(std::string("attachment; filename=") + value, name));
  EXPECT_TRUE(HttpDownloadFilename::parse("attachment; filename=COM10.epub", name));
}

TEST(HttpDownloadFilenameTest, LengthLimitsDoNotSilentlyTruncateIdentity) {
  std::string name;
  std::string maximum(235, 'a');
  maximum += ".epub";
  EXPECT_TRUE(HttpDownloadFilename::parse("attachment; filename=" + maximum, name));
  EXPECT_EQ(name, maximum);
  EXPECT_FALSE(HttpDownloadFilename::parse("attachment; filename=a" + maximum, name));
  EXPECT_FALSE(HttpDownloadFilename::parse(std::string(1025, 'a'), name));
}

INSTANTIATE_TEST_SUITE_P(Transports, HttpDownloaderTest,
                         testing::Values(HttpDownloader::Transport::ESP_HTTP, HttpDownloader::Transport::WOLFSSL));
TEST(HttpDownloadResumeTest, OverflowAndRetryBackstop) {
  size_t total = 0, end = 0;
  EXPECT_FALSE(HttpDownloadResume::range("bytes 0-999999999999999999999999/9999999999999999999999999", 0, 0, false, 0,
                                         total, end));
  EXPECT_FALSE(HttpDownloadResume::accepts(SIZE_MAX, 0, 1));
  HttpDownloadResume::RetryBudget budget;
  for (size_t i = 0; i < 19; ++i) EXPECT_TRUE(budget.again(i, i + 1));
  EXPECT_FALSE(budget.again(19, 20));
}

TEST_P(HttpDownloaderTest, DuplicateDispositionHeadersUseGeneratedFallback) {
  auto reply = named("First.epub");
  reply.extraDispositions = {"attachment; filename=Second.epub"};
  replies = {reply};
  ASSERT_EQ(serverDownload(), HttpDownloader::OK);
  EXPECT_EQ(resolved, "/Books/Author - Title.epub");
  EXPECT_EQ(files[resolved], "abc");
  EXPECT_FALSE(files.count("/Books/First.epub"));
  EXPECT_FALSE(files.count("/Books/Second.epub"));
}
