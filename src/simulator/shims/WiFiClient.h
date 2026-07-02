#pragma once
// Simulator shim: the crosspoint-simulator package aliases WiFiClient in WiFi.h
// but ships no WiFiClient.h; firmware code includes <WiFiClient.h> directly.
#include <WiFi.h>
