#pragma once

#include <Logging.h>
#include <WebServer.h>

#include <cstddef>
#include <cstring>

namespace core {

struct WebRouteSpec {
  const char* uri;
  HTTPMethod method;
  void (*fn)(WebServer*);
  void (*ufn)(WebServer*);
};

inline bool webRouteSpecMatches(const WebRouteSpec& spec, const HTTPMethod requestMethod, const char* requestUri) {
  if (spec.uri == nullptr || requestUri == nullptr || spec.fn == nullptr) {
    return false;
  }
  if (spec.method != HTTP_ANY && spec.method != requestMethod) {
    return false;
  }
  return std::strcmp(spec.uri, requestUri) == 0;
}

struct WebRouteEntry {
  const char* routeId;
  bool (*shouldRegister)();
  const WebRouteSpec* routes;
  size_t routeCount;
};

class WebRouteRegistry {
 public:
  static constexpr int kMaxEntries = 24;

  static void add(const WebRouteEntry& entry) {
    // cppcheck-suppress knownConditionTrueFalse
    if (count >= kMaxEntries) {
      LOG_ERR("REG", "WebRouteRegistry full (%d), entry dropped", kMaxEntries);
      return;
    }

    entries[count++] = entry;
  }

  static bool shouldRegister(const char* routeId) {
    const WebRouteEntry* entry = find(routeId);
    return entry != nullptr && entry->shouldRegister != nullptr && entry->shouldRegister();
  }

  static void forEachEnabledRoute(void (*visit)(const WebRouteSpec&, void*), void* ctx) {
    if (visit == nullptr) {
      return;
    }
    for (int i = 0; i < count; ++i) {
      if (entries[i].shouldRegister == nullptr || !entries[i].shouldRegister() || entries[i].routes == nullptr) {
        continue;
      }
      for (size_t r = 0; r < entries[i].routeCount; ++r) {
        visit(entries[i].routes[r], ctx);
      }
    }
  }

  static const WebRouteSpec* match(const HTTPMethod method, const char* uri) {
    for (int i = 0; i < count; ++i) {
      if (entries[i].shouldRegister == nullptr || !entries[i].shouldRegister() || entries[i].routes == nullptr) {
        continue;
      }
      for (size_t r = 0; r < entries[i].routeCount; ++r) {
        if (webRouteSpecMatches(entries[i].routes[r], method, uri)) {
          return &entries[i].routes[r];
        }
      }
    }
    return nullptr;
  }

#if defined(CROSSPOINT_HOST_BUILD) || defined(SIMULATOR)
  static void mountAll(WebServer* server) {
    if (server == nullptr) {
      return;
    }
    forEachEnabledRoute(
        [](const WebRouteSpec& spec, void* ctx) {
          auto* s = static_cast<WebServer*>(ctx);
          s->on(spec.uri, spec.method, [s, fn = spec.fn]() {
            if (fn != nullptr) {
              fn(s);
            }
          });
        },
        server);
  }
#endif

 private:
  static const WebRouteEntry* find(const char* routeId) {
    if (routeId == nullptr) {
      return nullptr;
    }

    for (int i = 0; i < count; ++i) {
      if (cStringsEqual(entries[i].routeId, routeId)) {
        return &entries[i];
      }
    }

    return nullptr;
  }

  static bool cStringsEqual(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
      return left == right;
    }
    return std::strcmp(left, right) == 0;
  }

  inline static WebRouteEntry entries[kMaxEntries] = {};
  inline static int count = 0;
};

}  // namespace core
