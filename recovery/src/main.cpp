// ForkDrift recovery shim — P2 skeleton.
//
// At this stage the shim does nothing functional: it boots, prints a
// banner over USB-CDC, and idles. The purpose of this skeleton is to
// confirm the build chain works end-to-end (toolchain, partitions,
// size budget) before P2 implementation lands.
//
// See plan-trmnl-coexistence.md, Phase P2 for the full feature set.

#include <Arduino.h>

namespace {

constexpr const char* BANNER = "ForkDrift recovery shim v0 (skeleton — no recovery logic yet)";

}  // namespace

void setup() {
  Serial.begin(115200);
  // Wait briefly for USB-CDC enumeration so the banner is visible.
  for (uint32_t start = millis(); !Serial && millis() - start < 1000;) {
    delay(10);
  }
  Serial.println(BANNER);
}

void loop() {
  // Idle. P2 implementation will replace this with: mount SD, parse
  // manifest, restore slots, set boot, restart.
  delay(1000);
}
