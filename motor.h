#pragma once

#include "config.h"

bool startMotor();
bool stopMotor();
void emergencyStopMotor();

bool motorConfigureForRun(uint16_t accelMs);
bool motorRun(bool running);
bool motorSetRuntimeSetpoint(uint16_t value);

bool changeControlMode(ControlMode controlMode);
bool changeTargetRpm(uint16_t rpm);
bool changeOpenLoopCommandRaw(uint16_t raw);
bool changeAccelerationMs(uint16_t accelerationMs);
bool changeDirection(MotorDirection direction);
bool changePolePairs(uint8_t pairs);
bool changeCurrentLimitAmps(uint8_t amps);
bool changePwmMaxPercent(uint8_t percent);
