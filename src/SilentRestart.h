#pragma once

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

bool serialOtaInProgress();
bool silentRestart(uint32_t target = 0);  // 0 = home screen, 1 = reader
bool silentRestartToReader();             // shorthand for silentRestart(1)

void recoverHeapAfterWifi(const char* tag);
