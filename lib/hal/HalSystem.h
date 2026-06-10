#pragma once

#include <cstdint>
#include <string>

namespace HalSystem {
struct StackFrame {
  uint32_t sp;
  uint32_t spp[8];
};

enum class ResetClass { Normal, Panic, InterruptWdt, TaskWdt, OtherWdt, Brownout };

void begin();

// Register a callback so getPanicInfo(true) can include a settings snapshot.
// Called once from main.cpp after settings are initialised.
void setSettingsProvider(std::string (*fn)());

// Dump panic info to SD card if necessary
void checkPanic();
void clearPanic();

std::string getPanicInfo(bool full = false);
bool isRebootFromPanic();
bool isRebootFromCrash();
ResetClass getResetClass();
std::string getResetClassString(ResetClass rc);
}  // namespace HalSystem
