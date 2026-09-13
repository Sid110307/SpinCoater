#pragma once

#include "config.h"
#include <Arduino.h>

enum class MotorStatus : uint16_t {
  Idle = 0,
  RunningClockwise = 1,
  RunningCounterClockwise = 2,
  EmergencyStop = 3,
};

struct RuntimeSnapshot {
  MotorDirection direction;
  bool motorRunning;
  bool emergencyLatched;
  bool bluetoothConnected;
};

struct PulseSpeedSnapshot {
  float rpm;
  float frequencyHz;
  uint32_t pulseCount;
  uint32_t lastPeriodUs;
  uint32_t minPeriodUs;
  uint32_t maxPeriodUs;
  uint32_t jitterUs;
  float averagePeriodUs;
  uint32_t ageMs;
  bool valid;
};

void setupHardware();

void setBluetoothConnected(bool connected);
bool isBluetoothConnected();

void setMotorDirection(MotorDirection direction);

void applyMotorRunState(bool running);
bool isMotorRunning();

void setEmergencyLatched(bool latched);
bool isEmergencyLatched();
bool clearEmergencyLatched();

bool physicalSafetyInputsActive();
bool physicalSafetyInputsInactiveFor(uint32_t stableMs);

RuntimeSnapshot getRuntimeSnapshot();
PulseSpeedSnapshot getPulseSpeedSnapshot();

/**
 Emergency stop: red
 Running: green
 Bluetooth connected (idle): cyan
 Idle (no BT): dim amber
 */
void updateStatusLed();
MotorStatus getMotorStatus();
const char *motorStatusToString(MotorStatus status);

void IRAM_ATTR handleEmergencyInterrupt();
void IRAM_ATTR handleSpeedPulseInterrupt();
