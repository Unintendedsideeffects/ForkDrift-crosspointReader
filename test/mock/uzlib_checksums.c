#include <stdint.h>

#include "uzlib.h"

uint32_t uzlib_adler32(const void* data, unsigned int length, uint32_t prev_sum) {
  const uint8_t* bytes = (const uint8_t*)data;
  uint32_t s1 = prev_sum & 0xFFFFu;
  uint32_t s2 = (prev_sum >> 16) & 0xFFFFu;
  const uint32_t mod = 65521u;

  for (unsigned int i = 0; i < length; ++i) {
    s1 += bytes[i];
    if (s1 >= mod) s1 -= mod;
    s2 += s1;
    if (s2 >= mod) s2 -= mod;
  }

  return (s2 << 16) | s1;
}

uint32_t uzlib_crc32(const void* data, unsigned int length, uint32_t crc) {
  static uint32_t table[256];
  static int inited = 0;

  if (!inited) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int j = 0; j < 8; ++j) {
        c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      table[i] = c;
    }
    inited = 1;
  }

  const uint8_t* bytes = (const uint8_t*)data;
  for (unsigned int i = 0; i < length; ++i) {
    crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc;
}
