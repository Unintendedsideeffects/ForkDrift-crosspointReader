#include "HalPowerManager.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include <cassert>

#include "HalGPIO.h"

HalPowerManager powerManager;  // Singleton instance

namespace {
// Voltage-based SoC on the X4 ADC path sags hard while the device is under load
// (e-ink refresh, full CPU frequency, WiFi). A single reading taken at the moment
// of a button press reports a falsely low charge. Take a few quick samples and use
// the median to reject the load-induced outliers. N is tiny: 5 uint16_t on the
// stack (10 bytes), manual insertion sort, no heap and no <algorithm> dependency.
uint16_t readPercentageMedian(const BatteryMonitor& battery) {
  constexpr int N = 5;
  uint16_t s[N];
  for (int i = 0; i < N; ++i) {
    s[i] = battery.readPercentage();
  }
  for (int i = 1; i < N; ++i) {
    const uint16_t v = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > v) {
      s[j + 1] = s[j];
      --j;
    }
    s[j + 1] = v;
  }
  return s[N / 2];
}
}  // namespace

void HalPowerManager::begin() {
  if (gpio.deviceIsX3()) {
    // X3 uses an I2C fuel gauge for battery monitoring.
    // I2C init must come AFTER gpio.begin() so early hardware detection/probes are finished.
    Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
    Wire.setTimeOut(4);
    _batteryUseI2C = true;
  } else {
    pinMode(BAT_GPIO0, INPUT);
  }
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
}

void HalPowerManager::setPowerSaving(bool enabled) {
  if (normalFreq <= 0) {
    return;  // invalid state
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    // Wifi is active, force disabling power saving
    enabled = false;
  }

  // Note: We don't use mutex here to avoid too much overhead,
  // it's not very important if we read a slightly stale value for lockCount
  const int count = lockCount;

  if (count == 0 && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      return;
    }
    isLowPower = true;

  } else if ((!enabled || count > 0) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
    if (!setCpuFrequencyMhz(normalFreq)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      return;
    }
    isLowPower = false;
  }

  // Otherwise, no change needed
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio) const {
  // Ensure that the power button has been released to avoid immediately turning back on if you're holding it
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
  // Pre-sleep routines from the original firmware
  // GPIO13 is connected to battery latch MOSFET, we need to make sure it's low during sleep
  // Note that this means the MCU will be completely powered off during sleep, including RTC
  constexpr gpio_num_t GPIO_SPIWP = GPIO_NUM_13;
  gpio_set_direction(GPIO_SPIWP, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_SPIWP, 0);
  esp_sleep_config_gpio_isolate();
  gpio_deep_sleep_hold_en();
  gpio_hold_en(GPIO_SPIWP);
  pinMode(InputManager::POWER_BUTTON_PIN, INPUT_PULLUP);
  // Arm the wakeup trigger *after* the button is released
  // Note: this is only useful for waking up on USB power. On battery, the MCU will be completely powered off, so the
  // power button is hard-wired to briefly provide power to the MCU, waking it up regardless of the wakeup source
  // configuration
  esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  // Enter Deep Sleep
  esp_deep_sleep_start();
}

uint16_t HalPowerManager::getBatteryPercentage() const {
  if (_batteryUseI2C) {
    const unsigned long now = millis();
    if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) {
      return _batteryCachedPercent;
    }

    // Read SOC directly from I2C fuel gauge (16-bit LE register).
    // On I2C error, keep last known value to avoid UI jitter/slowdowns.
    Wire.beginTransmission(I2C_ADDR_BQ27220);
    Wire.write(BQ27220_SOC_REG);
    if (Wire.endTransmission(false) != 0) {
      _batteryLastPollMs = now;
      return _batteryCachedPercent;
    }
    Wire.requestFrom(static_cast<uint8_t>(I2C_ADDR_BQ27220), static_cast<uint8_t>(2), static_cast<uint8_t>(true));
    if (Wire.available() < 2) {
      _batteryLastPollMs = now;
      return _batteryCachedPercent;
    }
    const uint8_t lo = Wire.read();
    const uint8_t hi = Wire.read();
    const uint16_t soc = (hi << 8) | lo;
    _batteryCachedPercent = soc > 100 ? 100 : soc;
    _batteryLastPollMs = now;
    return _batteryCachedPercent;
  }
  static const BatteryMonitor battery = BatteryMonitor(BAT_GPIO0);

  // Throttle ADC sampling exactly like the I2C path above: the status bar redraws
  // on every input/page-turn, so without this gate every button press feeds a
  // fresh (load-sagged) sample into the EMA and the displayed % chases input
  // activity instead of charge. On this path _batteryCachedPercent is stored at
  // 10x scale, so the cached early-return must divide by 10.
  const unsigned long now = millis();
  if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) {
    return _batteryCachedPercent / 10;
  }
  _batteryLastPollMs = now;

  const uint16_t sample = readPercentageMedian(battery);

  // smooth the battery %.
  if (_batteryCachedPercent == 0) {
    _batteryCachedPercent = 10 * sample;
  } else {
    _batteryCachedPercent = (_batteryCachedPercent * 9 + sample * 10) / 10;
  }
  return _batteryCachedPercent / 10;
}

HalPowerManager::Lock::Lock() {
  if (powerManager.modeMutex == nullptr) {
    LOG_ERR("PWR", "HalPowerManager used before begin(); skipping lock");
    valid = false;
    return;
  }

  held = (xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY) == pdTRUE);
  if (!held) {
    LOG_ERR("PWR", "Failed to take mode mutex");
    return;
  }
  powerManager.lockCount++;
  valid = true;
  xSemaphoreGive(powerManager.modeMutex);
  held = false;

  // Immediately restore normal CPU frequency if currently in low-power mode
  powerManager.setPowerSaving(false);
}

HalPowerManager::Lock::~Lock() {
  if (powerManager.modeMutex == nullptr) {
    return;
  }

  bool shouldReEnable = false;
  held = (xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY) == pdTRUE);
  if (!held) {
    LOG_ERR("PWR", "Failed to retake mode mutex during unlock");
    return;
  }
  if (valid) {
    if (powerManager.lockCount > 0) {
      powerManager.lockCount--;
    }
    shouldReEnable = (powerManager.lockCount == 0);
  }
  const TaskHandle_t self = xTaskGetCurrentTaskHandle();
  const TaskHandle_t holder = xSemaphoreGetMutexHolder(powerManager.modeMutex);
  if (holder != self) {
    LOG_ERR("PWR", "skip give (not holder): self='%s' holder='%s'", pcTaskGetName(self),
            holder ? pcTaskGetName(holder) : "<none>");
    return;
  }
  xSemaphoreGive(powerManager.modeMutex);
  held = false;

  if (shouldReEnable) {
    powerManager.setPowerSaving(true);
  }
}
