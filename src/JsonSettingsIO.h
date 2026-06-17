#pragma once

#include <HalStorage.h>

class CrossPointSettings;
class CrossPointState;
class WifiCredentialStore;
class RecentBooksStore;
class OpdsServerStore;
class ReadingStatsStore;

namespace JsonSettingsIO {

// CrossPointSettings
bool saveSettings(const CrossPointSettings& s, const char* path);
bool loadSettings(CrossPointSettings& s, const char* json, bool* needsResave = nullptr);
bool loadSettings(CrossPointSettings& s, HalFile& file, bool* needsResave = nullptr);

// CrossPointState
bool saveState(const CrossPointState& s, const char* path);
bool loadState(CrossPointState& s, const char* json);
bool loadState(CrossPointState& s, HalFile& file);

// WifiCredentialStore
bool saveWifi(const WifiCredentialStore& store, const char* path);
bool loadWifi(WifiCredentialStore& store, const char* json, bool* needsResave = nullptr);
bool loadWifi(WifiCredentialStore& store, HalFile& file, bool* needsResave = nullptr);

// RecentBooksStore
bool saveRecentBooks(const RecentBooksStore& store, const char* path);
bool loadRecentBooks(RecentBooksStore& store, const char* json);
bool loadRecentBooks(RecentBooksStore& store, HalFile& file);

// OpdsServerStore
bool saveOpds(const OpdsServerStore& store, const char* path);
bool loadOpds(OpdsServerStore& store, const char* json, bool* needsResave = nullptr);
bool loadOpds(OpdsServerStore& store, HalFile& file, bool* needsResave = nullptr);

// Reading stats
bool saveReadingStats(const ReadingStatsStore& store, const char* path);
bool loadReadingStats(ReadingStatsStore& store, const char* json);
bool loadReadingStats(ReadingStatsStore& store, HalFile& file);

}  // namespace JsonSettingsIO
