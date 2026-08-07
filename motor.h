#pragma once

#include "config.h"

bool startMotor();
void stopMotor();
void emergencyStopMotor();

bool changeControlMode(ControlMode controlMode);
bool changeTargetRpm(uint16_t rpm);
bool changeOpenLoopCommandRaw(uint16_t raw);
bool changeAccelerationMs(uint16_t accelerationMs);
bool changeDirection(MotorDirection direction);
