#pragma once

struct esp_partition_t {
  const char* label;
};

inline const esp_partition_t* esp_ota_get_next_update_partition(const void*) { return nullptr; }
inline int esp_ota_set_boot_partition(const esp_partition_t*) { return 0; }
inline void esp_restart() {}
