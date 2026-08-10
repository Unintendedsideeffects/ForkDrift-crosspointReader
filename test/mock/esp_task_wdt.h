#pragma once
// Host test stub — there is no task watchdog on the host, so feeding it is a no-op.
inline int esp_task_wdt_reset() { return 0; }
