#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace firmware_update {

uint32_t fnv1aUpdate(uint32_t hash, const uint8_t* data, const size_t length);

bool isFirmwareVersionChar(const char ch);

void advanceMarkerMatch(const char ch, const char* marker, size_t& matchLength);

std::string extractVersionAfterMarker(const uint8_t* data, size_t length, const char* marker);

}  // namespace firmware_update
