#pragma once

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

void silentRestart();          // home screen
void silentRestartToReader();  // currently-open EPUB (APP_STATE.openEpubPath)

// Fully de-inits WiFi (esp_wifi_stop + deinit, which returns the WiFi/LWIP heap)
// and then reboots to home ONLY when the largest contiguous block is still too
// small for later memory-heavy work (e.g. opening an EPUB). The ESP32 heap cannot
// be compacted, so a reboot is the only way to clear a live allocation wedged
// mid-heap; most sessions recover enough on teardown alone and return without
// rebooting. No-op when WiFi is already off. `tag` labels the diagnostic log.
void recoverHeapAfterWifi(const char* tag);
