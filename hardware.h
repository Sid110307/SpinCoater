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
  uint32_t lastEdgeUs;
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
PulseSpeedSnapshot getProfilePulseSpeedSnapshot();

/**
LED:
 Emergency stop: red
 Running: green
 Bluetooth connected (idle): cyan
 Idle (no BT): dim amber
Buzzer:
 Emergency: three short beeps every 1.5 s.
 Driver fault: one long beep every 2 s.
*/
void updateStatusIndicators();
MotorStatus getMotorStatus();
const char *motorStatusToString(MotorStatus status);

void handleEmergencyInterrupt();
void handleSpeedPulseInterrupt();
