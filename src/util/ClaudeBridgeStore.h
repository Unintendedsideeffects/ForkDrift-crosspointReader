#pragma once

#include <string>

/**
 * Singleton config store for the Claude Code question feature.
 *
 * Only a shared token lives here. The device is the server — Claude Code's
 * hook discovers it through the existing CrossPoint UDP protocol and pushes
 * questions to it, so there is no remote address for the device to know.
 * Setup is a JSON file dropped on the SD card root, imported into
 * /.crosspoint/claude_bridge.json on the next boot and then deleted
 * (consume-on-import, same contract as TerminusCredentialStore).
 *
 * JSON schema:
 *   { "token": "..." }
 *
 * A non-empty token is required. Without it the question routes fail closed.
 */
class ClaudeBridgeStore {
 public:
  static ClaudeBridgeStore& getInstance();

  // Load from SD card. Checks the drop path first, then the persisted path.
  bool load();
  bool save() const;
  void clear();

  const std::string& token() const { return token_; }
  void setToken(const std::string& v) { token_ = v; }

  static constexpr const char* kStoredPath = "/.crosspoint/claude_bridge.json";
  static constexpr const char* kDropPath = "/claude_bridge.json";

 private:
  ClaudeBridgeStore() = default;
  ClaudeBridgeStore(const ClaudeBridgeStore&) = delete;
  ClaudeBridgeStore& operator=(const ClaudeBridgeStore&) = delete;

  bool loadFromJson(const char* json);

  std::string token_;
};

#define CLAUDE_BRIDGE_STORE ClaudeBridgeStore::getInstance()
