#pragma once

#include <Arduino.h>
#include "config.h"

enum class MotorDirection : uint8_t {
	Clockwise = 0,
	CounterClockwise = 1,
};

enum class MotorStatus : uint8_t {
	Idle = 0,
	RunningClockwise = 1,
	RunningCounterClockwise = 2,
	EmergencyStop = 3,
	AlarmFault = 4,
};

void setupHardware();
void serviceHardware();

void setBluetoothConnected(bool connected);
bool isBluetoothConnected();

void setMotorDirection(MotorDirection direction);
MotorDirection getMotorDirection();

void applyMotorRunState(bool running);
bool isMotorRunning();

void setMotorPolePairs(int polePairs);
int getMotorPolePairs();

void setEmergencyLatched(bool latched);
bool isEmergencyLatched();
void clearEmergencyLatched();

void setAlarmLatched(bool latched);
bool isAlarmLatched();
void clearAlarmLatched();

/**
Emergency stop: red
Alarm fault: orange
Running clockwise: green
Running counter-clockwise: blue
Bluetooth connected (idle): cyan
Idle: off
*/
void updateStatusLed();
MotorStatus getMotorStatus();
const char* motorStatusToString(MotorStatus status);

uint32_t getSpeedPulseCount();
void resetSpeedPulseCount();
float sampleSpeedRpm();
float getLastMeasuredSpeedRpm();

void IRAM_ATTR handleEmergencyInterrupt();
void IRAM_ATTR handleAlarmInterrupt();
void IRAM_ATTR handleSpeedInterrupt();
