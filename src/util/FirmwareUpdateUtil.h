#pragma once

#include <string>

class GfxRenderer;
class MappedInputManager;

class FirmwareUpdateUtil {
 public:
  static constexpr const char* kFirmwareBinPath = "/firmware.bin";

  /**
   * Checks if a firmware update file exists on the SD card.
   * @return true if a supported firmware .bin exists.
   */
  static bool checkForLocalUpdate();

  /**
   * Handles the boot-time local firmware update flow.
   * When a file is present, the user can install it immediately, skip once,
   * suppress prompts for the current firmware version, or delete the file.
   */
  static void handleLocalUpdateBootFlow(GfxRenderer& renderer, MappedInputManager& mappedInput);

  /**
   * Performs the firmware update from the detected local firmware file.
   * Displays progress using the provided renderer.
   * @param renderer The renderer to use for progress display.
   * @return true if the update was successful (the device will reboot on success).
   */
  static bool performLocalUpdate(const GfxRenderer& renderer);
};
