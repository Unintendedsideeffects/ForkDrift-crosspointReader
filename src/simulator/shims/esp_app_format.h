#pragma once
// Simulator shims: esp_app_format.h plus the OTA-update surface that the
// crosspoint-simulator package's esp_ota_ops.h stub does not cover. Local
// firmware flashing is meaningless in the simulator; everything here is an
// inert success/no-op so FirmwareUpdateUtil compiles and degrades gracefully.
#include <cstdint>

#include "esp_err.h"
#include "esp_partition.h"

#define ESP_APP_DESC_MAGIC_WORD 0xABCD5432

typedef struct {
  uint32_t magic_word;
  uint32_t secure_version;
  uint32_t reserv1[2];
  char version[32];
  char project_name[32];
  char time[16];
  char date[16];
  char idf_ver[32];
  uint8_t app_elf_sha256[32];
  uint32_t reserv2[20];
} esp_app_desc_t;

// Sizes only matter for file-offset math when sniffing a .bin on "SD".
typedef struct {
  uint8_t bytes[24];
} esp_image_header_t;
typedef struct {
  uint8_t bytes[8];
} esp_image_segment_header_t;

typedef uint32_t esp_ota_handle_t;
#define OTA_WITH_SEQUENTIAL_WRITES 0xFFFFFFFF

inline esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t* handle) {
  if (handle) *handle = 1;
  return ESP_OK;
}
inline esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t) { return ESP_OK; }
inline esp_err_t esp_ota_end(esp_ota_handle_t) { return ESP_OK; }
inline esp_err_t esp_ota_abort(esp_ota_handle_t) { return ESP_OK; }

inline const char* esp_err_to_name(esp_err_t) { return "ESP_ERR(sim)"; }
