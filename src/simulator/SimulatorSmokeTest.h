#pragma once

#ifdef SIMULATOR
void runSimulatorSmokeTestTick();

// Called from the safe-mode branch of the main loop (which returns before the
// normal smoke tick). Passes when SD failure was requested, fails otherwise.
void runSimulatorSmokeTestSafeModeTick();
#endif
