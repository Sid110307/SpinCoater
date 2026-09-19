#pragma once

#include <Arduino.h>

bool modbusBegin(uint16_t slaveAddress = 1);

bool modbusUseCommunicationControl();
bool modbusSetControlMode(bool closedLoop);
bool modbusSetClosedLoopRpm(uint16_t rpm);
bool modbusSetOpenLoopSpeedPercent(uint16_t percent);
bool modbusSetAccelTimeMs(uint16_t ms);
bool modbusSetRun(bool running);
bool modbusSetDirection(bool clockwise);
bool modbusSetBrake(bool brakeOn);
bool modbusClearFault();
bool modbusSetPolePairs(uint8_t pairs);
bool modbusSetCurrentLimit(uint8_t amps);
bool modbusSetPwmMax(uint8_t percent);

bool modbusReadOvercurrentAlarm(uint16_t &value);

bool modbusReadVoltage(uint16_t &value);
bool modbusReadCurrent(uint16_t &value);
bool modbusReadPower(uint16_t &value);
bool modbusReadSpeed(uint16_t &value);
bool modbusReadFault(uint16_t &value);
