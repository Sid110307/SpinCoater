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

void setupHardware();

void setBluetoothConnected(bool connected);
bool isBluetoothConnected();

void setMotorDirection(MotorDirection direction);
MotorDirection getMotorDirection();

void applyMotorRunState(bool running);
bool isMotorRunning();

void setEmergencyLatched(bool latched);
bool isEmergencyLatched();
void clearEmergencyLatched();

RuntimeSnapshot getRuntimeSnapshot();

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
