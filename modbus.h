#pragma once

#include <Arduino.h>

bool modbusBegin(uint16_t slaveAddress = 1);
bool modbusIsReady();

bool modbusUseCommunicationControl();
bool modbusUseIoControl();
bool modbusSetControlMode(bool closedLoop);
bool modbusSetClosedLoopRpm(uint16_t rpm);
bool modbusSetOpenLoopSpeedPercent(uint16_t percent);
bool modbusSetAccelTimeMs(uint16_t ms);
bool modbusSetRun(bool running);
bool modbusSetDirection(bool clockwise);
bool modbusSetBrake(bool brakeOn);
bool modbusClearFault();

bool modbusReadVoltage(uint16_t &value);
bool modbusReadCurrent(uint16_t &value);
bool modbusReadPower(uint16_t &value);
bool modbusReadSpeed(uint16_t &value);
bool modbusReadFault(uint16_t &value);
bool modbusReadBlock(uint16_t startReg, uint8_t count, uint16_t *out);

void modbusGetStats(uint32_t &count, uint32_t &timeouts, uint32_t &crcErrors,
                    uint32_t &lastUs, uint32_t &minUs, uint32_t &maxUs,
                    uint32_t &avgUs);
void modbusResetStats();
