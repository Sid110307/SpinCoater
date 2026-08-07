#pragma once

#include "config.h"
#include <Arduino.h>

enum class MotorStatus : uint16_t {
  Idle = 0,
  RunningClockwise = 1,
  RunningCounterClockwise = 2,
  EmergencyStop = 3,
};

void setupHardware();
void serviceHardware();

void setBluetoothConnected(bool connected);
bool isBluetoothConnected();

void setMotorDirection(MotorDirection direction);
MotorDirection getMotorDirection();

void applyMotorRunState(bool running);
bool isMotorRunning();

void setEmergencyLatched(bool latched);
bool isEmergencyLatched();
void clearEmergencyLatched();

/**
 Emergency stop: red
 Running clockwise: green
 Running counter-clockwise: blue
 Bluetooth connected (idle): cyan
 Idle: off
 */
void updateStatusLed();
MotorStatus getMotorStatus();
const char *motorStatusToString(MotorStatus status);

void IRAM_ATTR handleEmergencyInterrupt();
