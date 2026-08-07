#pragma once

#include <Arduino.h>
#include <Preferences.h>

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
static constexpr uint16_t SPEED = 2;
static constexpr uint16_t LIMIT_SWITCH = 14;
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
static constexpr uint32_t TIMER_FREQUENCY = 1000000;    // 1 MHz
static constexpr uint32_t TIMER_INTERVAL_US = 1000000;  // 1,000,000 us = 1s

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
