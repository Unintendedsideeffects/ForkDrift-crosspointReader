#include <gtest/gtest.h>

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
