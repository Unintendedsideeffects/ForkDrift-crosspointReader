#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * WiFi credential storage, shared between UI activities and the background
 * WiFi/web tasks.
 *
 * Thread-safety contract:
 *   - `credentialsMutex` guards the in-memory vectors only. It is NEVER held
 *     across an SD write or a cache invalidation: ForkDrift's storage path
 *     takes SpiBusMutex internally, so holding this mutex across it would
 *     invert lock order against the background tasks and deadlock. Every
 *     mutator therefore snapshots/mutates under the lock, releases, and only
 *     then persists.
 *   - Accessors return values (or std::optional), never references or pointers
 *     into the shared vector, which another task can reallocate.
 *
 * On-disk contract:
 *   - Passwords are XOR-obfuscated with the device MAC and base64-encoded (not
 *     cryptographically secure; it ties credentials to one device and prevents
 *     casual reading).
 *   - Each entry also persists the plaintext length and a CRC-32 so a
 *     decodable-but-corrupted value is discarded rather than tried as if it
 *     were the real password.
 *   - Entries written by older firmware carry neither field. Those load
 *     unchanged and are flagged for rewrite, so an upgrade never loses a
 *     working network.
 */

struct WifiCredential {
  std::string ssid;
  std::string password;  // Plaintext in memory; obfuscated with hardware key on disk
};

struct WifiCredentialSummary {
  std::string ssid;
  bool hasPassword = false;
  bool isLastConnected = false;
};

struct WifiCredentialSnapshot {
  std::vector<WifiCredential> credentials;
  std::string lastConnectedSsid;
};

namespace credential_integrity {

// IEEE CRC-32, bitwise so it costs no lookup table in DRAM. Detects accidental
// corruption of a stored credential; it is not authentication and does not make
// the XOR obfuscation secure.
constexpr uint32_t crc32(const std::string_view data) {
  uint32_t crc = 0xFFFFFFFFU;
  for (const char value : data) {
    crc ^= static_cast<uint8_t>(value);
    for (unsigned bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

constexpr bool validate(const std::string_view data, const size_t expectedLength, const uint32_t expectedCrc32) {
  return data.size() == expectedLength && crc32(data) == expectedCrc32;
}

}  // namespace credential_integrity

// Password encoding is injected so the JSON layer can be exercised on the host,
// where the hardware-key obfuscation (esp_efuse_mac / mbedtls) does not exist.
// Function pointers rather than std::function: no heap, no per-signature bloat.
struct WifiPasswordCodec {
  std::string (*encode)(const std::string& plaintext);
  std::string (*decode)(const char* encoded, bool* ok);
};

class WifiCredentialStore;
namespace JsonSettingsIO {
bool saveWifi(const WifiCredentialStore& store, const char* path);
bool loadWifi(WifiCredentialStore& store, const char* json, bool* needsResave);
bool loadWifi(WifiCredentialStore& store, HalFile& file, bool* needsResave);
}  // namespace JsonSettingsIO

class WifiCredentialStore {
 private:
  static WifiCredentialStore instance;
  std::vector<WifiCredential> credentials;
  std::string lastConnectedSsid;
  // std::mutex (not a FreeRTOS semaphore) so the host-test and simulator builds
  // compile unchanged; on ESP32 it maps to a FreeRTOS mutex anyway.
  mutable std::mutex credentialsMutex;

  // Private constructor for singleton
  WifiCredentialStore() = default;

  void adoptSnapshot(WifiCredentialSnapshot&& incoming);

  friend bool JsonSettingsIO::saveWifi(const WifiCredentialStore&, const char*);
  friend bool JsonSettingsIO::loadWifi(WifiCredentialStore&, const char*, bool*);
  friend bool JsonSettingsIO::loadWifi(WifiCredentialStore&, HalFile&, bool*);

 public:
  static constexpr size_t MAX_NETWORKS = 8;
  static constexpr size_t MAX_PASSWORD_LENGTH = 64;

  // Delete copy constructor and assignment
  WifiCredentialStore(const WifiCredentialStore&) = delete;
  WifiCredentialStore& operator=(const WifiCredentialStore&) = delete;

  // Get singleton instance
  static WifiCredentialStore& getInstance() { return instance; }

  // Save/load from SD card
  bool saveToFile() const;
  bool loadFromFile();

  // Consistent copy of the whole store, taken under the lock
  WifiCredentialSnapshot snapshot() const;

  // Credential management
  bool addCredential(const std::string& ssid, const std::string& password);
  bool removeCredential(const std::string& ssid);
  std::optional<WifiCredential> findCredential(const std::string& ssid) const;

  // Get all stored credentials (for UI display)
  std::vector<WifiCredential> getCredentials() const;

  // Password-free view for display/API/log consumers
  std::vector<WifiCredentialSummary> getCredentialSummaries() const;

  // Check if a network is saved
  bool hasSavedCredential(const std::string& ssid) const;

  // Last connected network
  void setLastConnectedSsid(const std::string& ssid);
  std::string getLastConnectedSsid() const;
  void clearLastConnectedSsid();

  // Clear all credentials
  void clearAll();
};

namespace wifi_credentials {

std::string serialize(const WifiCredentialSnapshot& snapshot, const WifiPasswordCodec& codec);

// Parses a credentials document. Entries failing the length/CRC check or
// exceeding MAX_PASSWORD_LENGTH are discarded individually; entries predating
// the integrity fields are accepted and set *needsResave so the caller can
// rewrite the file in the current format.
bool parse(const char* json, const WifiPasswordCodec& codec, WifiCredentialSnapshot& out, bool* needsResave);

// Human-readable, guaranteed to contain no password material.
std::string describe(const WifiCredentialSnapshot& snapshot);

}  // namespace wifi_credentials

// Helper macro to access credentials store
#define WIFI_STORE WifiCredentialStore::getInstance()
