#pragma once

#include <Arduino.h>
#include <InputManager.h>

// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define SPI_MISO 7  // SPI MISO, shared between SD card and display (Master In Slave Out)

#define BAT_GPIO0 0  // Battery voltage

#define UART0_RXD 20  // Used for USB connection detection

// Xteink X3 Hardware
#define X3_I2C_SDA 20
#define X3_I2C_SCL 0
#define X3_I2C_FREQ 400000

// TI BQ27220 Fuel gauge I2C
#define I2C_ADDR_BQ27220 0x55  // Fuel gauge I2C address
#define BQ27220_SOC_REG 0x2C   // StateOfCharge() command code (%)
#define BQ27220_CUR_REG 0x0C   // Current() command code (signed mA)
#define BQ27220_VOLT_REG 0x08  // Voltage() command code (mV)

// Analog DS3231 RTC I2C
#define I2C_ADDR_DS3231 0x68  // RTC I2C address
#define DS3231_SEC_REG 0x00   // Seconds command code (BCD)

// QST QMI8658 IMU I2C
#define I2C_ADDR_QMI8658 0x6B        // IMU I2C address
#define I2C_ADDR_QMI8658_ALT 0x6A    // IMU I2C fallback address
#define QMI8658_WHO_AM_I_REG 0x00    // WHO_AM_I command code
#define QMI8658_WHO_AM_I_VALUE 0x05  // WHO_AM_I expected value

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;

  // Input polling task state. The task samples inputMgr at a fixed rate so
  // short presses aren't lost when the main loop slows down for power saving.
  TaskHandle_t inputTaskHandle = nullptr;
  volatile bool inputTaskStopRequested = false;
  portMUX_TYPE inputMux = portMUX_INITIALIZER_UNLOCKED;
  // Event bits accumulated by the poll task, drained by update().
  uint8_t pendingPressed = 0;
  uint8_t pendingReleased = 0;

  // With InputManager's 5ms debounce this reliably catches taps >= ~35ms.
  static constexpr uint32_t INPUT_TASK_PERIOD_MS = 15;

  static void inputTaskTrampoline(void* param);
  void inputTaskLoop();
#endif

  // Per-main-loop-cycle event snapshots read by wasPressed()/wasReleased().
  uint8_t pressedSnapshot = 0;
  uint8_t releasedSnapshot = 0;

  bool lastUsbConnected = false;
  bool usbStateChanged = false;

 public:
  enum class DeviceType : uint8_t { X4, X3 };

 private:
  DeviceType _deviceType = DeviceType::X4;

 public:
  HalGPIO() = default;

  // Inline device type helpers for cleaner downstream checks
  inline bool deviceIsX3() const { return _deviceType == DeviceType::X3; }
  inline bool deviceIsX4() const { return _deviceType == DeviceType::X4; }

  // Start button GPIO and setup SPI for screen and SD card
  void begin();

  // Start/stop the fixed-rate input polling task. While the task runs,
  // update() drains events accumulated by the task instead of sampling
  // directly, so presses shorter than one main-loop cycle are not dropped.
  // Must be started only after the setup-time direct polling loops
  // (verifyPowerButtonWakeup etc.) are done, and stopped before any code
  // that resumes calling update() in a tight loop around deep sleep.
#if CROSSPOINT_EMULATED == 0
  void startInputTask();
  void stopInputTask();
#else
  void startInputTask() {}
  void stopInputTask() {}
#endif

  // Button input methods
  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  unsigned long getPowerButtonHeldTime() const;

  // Setup wake up GPIO and enter deep sleep
  void startDeepSleep();

  // Verify power button was held long enough after wakeup.
  // If verification fails, enters deep sleep and does not return.
  // Should only be called when wakeup reason is PowerButton.
  void verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed);

  // Check if USB is connected
  bool isUsbConnected() const;

  // Returns true once per edge (plug or unplug) since the last update()
  bool wasUsbStateChanged() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  // Button indices
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
};

extern HalGPIO gpio;
