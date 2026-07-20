#include "OpdsServerStore.h"
#include "core/features/KoreaderOpdsBridge.h"
#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"

namespace {

// We'll set static strings in the test case and use these lambdas to return them
std::string mockUsername;
std::string mockPassword;
std::string mockServerUrl;

std::string getMockUsername() { return mockUsername; }
std::string getMockPassword() { return mockPassword; }
std::string getMockServerUrl() { return mockServerUrl; }

void resetMocks() {
  mockUsername.clear();
  mockPassword.clear();
  mockServerUrl.clear();
  core::test_hooks::getKoreaderUsername = nullptr;
  core::test_hooks::getKoreaderPassword = nullptr;
  core::test_hooks::getKoreaderServerUrl = nullptr;
}

void setupMocks(const std::string& user, const std::string& pass, const std::string& url) {
  mockUsername = user;
  mockPassword = pass;
  mockServerUrl = url;
  core::test_hooks::getKoreaderUsername = getMockUsername;
  core::test_hooks::getKoreaderPassword = getMockPassword;
  core::test_hooks::getKoreaderServerUrl = getMockServerUrl;
}

}  // namespace

TEST_CASE("effectiveOpdsCredentials fallback behavior") {
  // Reset/mock storage mock
  Storage.reset();
  resetMocks();

  SUBCASE("Case 1: Server has own credentials -> returned unchanged") {
    OpdsServer server{"My Server", "http://myopdsserver.com/feed.xml", "server_user", "server_pass"};

    // Even if KOReader has credentials set, server's own credentials should be prioritized
    setupMocks("ko_user", "ko_pass", "http://myopdsserver.com/api/koreader");

    auto creds = core::effectiveOpdsCredentials(server);
    CHECK(creds.username == "server_user");
    CHECK(creds.password == "server_pass");
  }

  SUBCASE("Case 2: Server credentials empty + KOReader credentials set for same host -> KOReader pair returned") {
    OpdsServer server{"My Server", "http://myopdsserver.com/feed.xml", "", ""};
    setupMocks("ko_user", "ko_pass", "http://myopdsserver.com/api/koreader");

    auto creds = core::effectiveOpdsCredentials(server);
    CHECK(creds.username == "ko_user");
    CHECK(creds.password == "ko_pass");
  }

  SUBCASE("Case 3: Server credentials empty + KOReader credentials set but different host -> empty") {
    OpdsServer server{"My Server", "http://myopdsserver.com/feed.xml", "", ""};
    setupMocks("ko_user", "ko_pass", "http://anotherhost.com/api/koreader");

    auto creds = core::effectiveOpdsCredentials(server);
    CHECK(creds.username == "");
    CHECK(creds.password == "");
  }

  SUBCASE("Case 4: Both empty -> empty") {
    OpdsServer server{"My Server", "http://myopdsserver.com/feed.xml", "", ""};
    resetMocks();

    auto creds = core::effectiveOpdsCredentials(server);
    CHECK(creds.username == "");
    CHECK(creds.password == "");
  }
}
