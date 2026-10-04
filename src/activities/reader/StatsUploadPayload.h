#pragma once

#include <cstddef>

#include "BookReadingStats.h"
#include "GlobalReadingStats.h"

// One snapshot at a time, with no JSON tree or library-sized allocation.
namespace StatsUploadPayload {
constexpr size_t CAPACITY = 1536;
bool global(char* out, size_t capacity, const char* deviceId, const GlobalReadingStats& stats,
            uint32_t dailyDay = UINT32_MAX, uint32_t dailySeconds = 0);
bool book(char* out, size_t capacity, const char* deviceId, const char* document, const BookReadingStats& stats);
}  // namespace StatsUploadPayload
