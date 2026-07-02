#pragma once
// Simulator shim: esp_app_format.h is not mocked by crosspoint-simulator.
#include <cstdint>
typedef struct { uint32_t magic_word; char version[32]; char project_name[32]; } esp_app_desc_t;
