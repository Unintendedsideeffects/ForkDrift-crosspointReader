#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "util/RecentBooksStore.h"

struct HarnessTeamMember {
  int speciesId = 0;
  std::string name;
};

std::filesystem::path resolveDeviceFilesystemRoot();
bool mountDeviceFilesystem(std::filesystem::path& mountedRoot);
std::vector<RecentBook> loadRecentBooksFromDevice();
bool applySettingsFromDevice();
const std::vector<HarnessTeamMember>& harnessTeamMembers();
int harnessBookIndex(const std::string& bookPath);
