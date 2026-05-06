# Testing and Debugging

CrossPoint runs on real hardware, so debugging usually combines local build checks and on-device logs.

## Local Static Analysis

Before flashing, run the same local checks used by the current workflows:

```sh
uv run ./bin/clang-format-fix
uv run pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
bash test/run_host_tests.sh
python3 scripts/validate_contract_server.py
uv run pio run
```

## Logging and Monitoring

### Logging Macros

We use a structured logging system defined in `lib/Logging/Logging.h`. Avoid using `Serial.print()` directly. Instead, use:

- `LOG_ERR(origin, format, ...)`: For critical errors.
- `LOG_INF(origin, format, ...)`: For general information (e.g., activity transitions).
- `LOG_WRN(origin, format, ...)`: For warnings that don't stop execution.
- `LOG_DBG(origin, format, ...)`: For detailed debugging information.

The `origin` parameter is a short string identifying the module (e.g., `"EPUB"`, `"WIFI"`).

### Standard Serial Monitor

```sh
uv run pio device monitor
```

### On-Device Developer Mode

Settings -> System -> Developer Mode mirrors firmware logs to `/crosspoint-debug.log`
on the SD card. This is useful when WiFi file transfer or other on-device flows
are slow and serial capture is not practical.

Developer Mode can only write log calls that are present in the firmware image;
serial-enabled builds keep `LOG_INF`, `LOG_WRN`, and `LOG_DBG` available for the
runtime switch, while slim builds strip logging with `-UENABLE_SERIAL_LOG`.

### Enhanced Debugging Monitor

For a more powerful monitoring experience, including colorized logs and potential visualization, use the custom monitor script:

```sh
# Ensure dependencies are installed
python3 -m pip install pyserial colorama matplotlib

# Run the monitor
python3 scripts/debugging_monitor.py
```

## Resource Monitoring

Memory management is critical on the ESP32-C3. Use these calls to monitor resource usage:

- **Heap Usage**: `ESP.getFreeHeap()` returns the available heap memory in bytes.
- **Stack Usage**: `uxTaskGetStackHighWaterMark(NULL)` returns the minimum amount of remaining stack space (in words) that has been available since the task started. A low value (e.g., < 200) indicates a potential stack overflow.

## Troubleshooting

### Cache Invalidation

If you notice inconsistent behavior in the reader or library, try clearing the on-disk cache on the SD card:

```sh
# Remove the cache directory
rm -rf /path/to/sd/.crosspoint/
```

The firmware will automatically recreate the necessary structures on the next boot.

### CI Workflows

The project uses GitHub Actions for Continuous Integration. Every PR triggers several workflows:
- **Build**: Verifies that the code compiles for the target hardware.
- **CI (build)**: Runs formatting, host tests, contract validation, and static analysis checks.
- **Feature Matrix**: Tests various combinations of build flags.
- **PR Formatting Check**: Verifies the repository formatting rules.

You can view the status of these workflows in the **Actions** tab of the repository.

## Useful Bug Report Contents

- **Firmware version**: Look for the version string in the boot logs.
- **Exact steps**: Provide a numbered list of actions leading to the issue.
- **Serial logs**: Capture logs from boot through the failure point.
- **Crash report**: If the device panicked, check `/crash_report.txt` on the SD card.
