#pragma once

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * RecoveryMenuActivity
 *
 * Entry point for the boot recovery mode (hold left side button + power at
 * boot, see main.cpp). Presents a minimal, dependency-light menu of last-resort
 * maintenance actions for when the device has become unusable through bad
 * settings or corrupt cache:
 *   - Flash firmware from the SD card
 *   - Clear the reading cache
 *   - Reset settings to defaults
 *   - Factory reset (clears metadata + cache, keeps books/images/notes)
 *   - Restart the device (clean reboot back into the normal UI)
 *
 * Each action reuses the existing maintenance activity as a sub-activity, so
 * the recovery path inherits their warning/confirm/result flows. The firmware
 * flasher is launched in non-recovery mode here so its Back returns to this
 * menu instead of trapping. All actions operate on /.crosspoint/ on the SD, so
 * (like recovery mode itself) they require a mounted SD card.
 */
class RecoveryMenuActivity final : public Activity {
 public:
  explicit RecoveryMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RecoveryMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

  // Recovery is a standalone maintenance screen; the normal chrome is hidden to
  // avoid depending on app state that may be exactly what is broken.
  bool showsGlobalStatusBar() const override { return false; }

 private:
  // Order matters: indices map 1:1 to the rendered list and activateSelected().
  enum Item : int { FirmwareUpdate, ClearCache, ResetSettings, FactoryReset, Restart, ItemCount };

  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  static std::string itemLabel(int index);
  void activateSelected();
};
