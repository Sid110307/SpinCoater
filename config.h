#pragma once

#include <Arduino.h>
#include <Preferences.h>

#ifndef FIRMWARE_VERSION
#error "FIRMWARE_VERSION must be defined in platformio.ini"
#endif

enum class MotorDirection : uint8_t {
  Clockwise = 0,
  CounterClockwise = 1,
};

enum class ControlMode : uint8_t {
  OpenLoop = 0,
  ClosedLoop = 1,
};

extern Preferences prefs;

namespace Pins {
  static constexpr uint16_t EMERGENCY = 15;
  static constexpr uint16_t SPEED = 14;
  static constexpr uint16_t LIMIT_SWITCH = 27;
  static constexpr uint16_t BUZZER = 4;
  static constexpr uint16_t RS485_RX = 16;
  static constexpr uint16_t RS485_TX = 17;
  static constexpr uint16_t RS485_DE_RE = 5;
  static constexpr uint16_t LED_R = 21;
  static constexpr uint16_t LED_G = 19;
  static constexpr uint16_t LED_B = 18;
}

static constexpr uint8_t DEFAULT_MODBUS_ADDRESS = 1;
static constexpr ControlMode DEFAULT_CONTROL_MODE = ControlMode::OpenLoop;
static constexpr MotorDirection DEFAULT_DIRECTION = MotorDirection::Clockwise;
static constexpr uint16_t DEFAULT_TARGET_RPM = 1200;
static constexpr uint16_t DEFAULT_OPEN_LOOP_COMMAND_RAW = 405;
static constexpr uint16_t DEFAULT_ACCELERATION_MS = 300;

static constexpr uint32_t CONFIG_VERSION = 4;  // Change if config params update

static constexpr int DEBOUNCE_MS = 200;
static constexpr uint32_t SAFETY_CLEAR_STABLE_MS = 200;
static constexpr float TIMER_FREQUENCY = 1000000.0f;

static constexpr uint8_t SPEED_POLE_PAIRS = 4;
static constexpr uint8_t SPEED_SAMPLE_WINDOW = 32;
static constexpr uint32_t SPEED_GLITCH_REJECT_US = 50;
static constexpr uint32_t SPEED_STALE_TIMEOUT_MS = 500;
static constexpr uint8_t PROFILE_SPEED_AVERAGE_PERIODS = 4;
static constexpr uint16_t PROFILE_SAMPLE_PERIOD_MS = 20;
static constexpr uint8_t PROFILE_ACCEL_WINDOW_SAMPLES = 5;
static constexpr uint16_t PROFILE_BUFFER_SAMPLES = 256;
static constexpr uint8_t PROFILE_EXPORT_BATCH_SAMPLES = 48;

#ifndef BYPASS_SAFETY
#define BYPASS_SAFETY 0
#endif

inline constexpr bool safetyBypassEnabled() {
  return BYPASS_SAFETY != 0;
}

static constexpr uint16_t CLOSED_LOOP_RPM_MIN = 200;
static constexpr uint16_t CLOSED_LOOP_RPM_MAX = 4000;
static constexpr uint16_t OPEN_LOOP_RAW_MIN = 0;
static constexpr uint16_t OPEN_LOOP_RAW_MAX = 1000;
static constexpr uint16_t ACCEL_MS_MIN = 300;
static constexpr uint16_t ACCEL_MS_MAX = 15000;

static constexpr uint8_t RAMP_MAX_SEGMENTS = 32;
static constexpr uint16_t RAMP_TICK_MS = 20;

static constexpr uint16_t TELEMETRY_PERIOD_MS = 200;
static constexpr uint16_t LED_REFRESH_MS = 50;

struct MotorConfig {
  uint32_t version;

  uint8_t modbusAddress;
  ControlMode controlMode;
  MotorDirection direction;

  uint16_t targetRpm;
  uint16_t openLoopCommandRaw;
  uint16_t accelerationMs;
};

extern MotorConfig config;

void loadConfig();
void saveConfig();
void setDefaultConfig();
