#pragma once

#include <cstdint>
#include <string>

#include "StatsUploadClient.h"

struct Clipping;

namespace ClippingsUpload {
// Server identity: first 16 lowercase hex characters of SHA256(decimal timestamp + exact UTF-8 text).
bool identity(uint32_t timestamp, const std::string& text, char (&id)[17]);
StatsUploadClient::Result upload(const std::string& path, const std::string& document);
}  // namespace ClippingsUpload
