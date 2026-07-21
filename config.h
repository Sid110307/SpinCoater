#pragma once

#include <Arduino.h>
#include <Preferences.h>

extern Preferences prefs;

namespace Pins {
  static constexpr uint8_t PWM = 13;
  static constexpr uint8_t ALM = 14;
  static constexpr uint8_t SPEED = 27;
  static constexpr uint8_t BRK = 26;
  static constexpr uint8_t DIR = 25;
  static constexpr uint8_t EN = 33;
  static constexpr uint8_t RS485_RX = 16;
  static constexpr uint8_t RS485_TX = 17;
  static constexpr uint8_t RS485_DE_RE = 5;
  static constexpr uint8_t EMERGENCY = 23;
  static constexpr uint8_t LED_R = 21;
  static constexpr uint8_t LED_G = 19;
  static constexpr uint8_t LED_B = 18;
}

static constexpr int DEFAULT_FREQ = 10000;
static constexpr float DEFAULT_DUTY = 40.5;
static constexpr int DEFAULT_PWM_PIN = Pins::PWM;
static constexpr int DEFAULT_POLE_PAIRS = 4;
static constexpr int MAX_FREQ = 40000000;

static constexpr int MAX_PASSES = 5;
static constexpr int CONFIG_VERSION = 2;  // Change if config params update

static constexpr int DEBOUNCE_MS = 200;
static constexpr uint32_t TIMER_FREQUENCY = 1000000;    // 1 MHz
static constexpr uint32_t TIMER_INTERVAL_US = 1000000;  // 1,000,000 us = 1s

bool isValidMotorPwmPin(int pin);

struct Pass {
  float rpm;
  float accel;
  float delay;
};

struct MotorConfig {
  int version;
  int frequency;
  float dutyCycle;
  float length;
  float duration;
  int pwmPin;
  int polePairs;
  int numPasses;
  Pass passes[MAX_PASSES];
};

extern MotorConfig config;

void loadConfig();
void saveConfig();
void setDefaultConfig();
