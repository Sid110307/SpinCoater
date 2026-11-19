#pragma once

#include <freertos/FreeRTOS.h>
#include "driver_packet.h"

enum class MbOp : uint8_t {
  CommSource,
  ControlMode,
  ClosedLoopRpm,
  OpenLoopPercent,
  AccelMs,
  Run,
  Direction,
  Brake,
  ClearFault,
  PolePairs,
  CurrentLimit,
  PwmMax,
  RefreshTelemetry,
  RefreshOvercurrentAlarm,
  DriverPacket
};

struct Telemetry {
  uint16_t voltage = 0;
  uint16_t current = 0;
  uint16_t power = 0;
  uint16_t speed = 0;
  uint16_t fault = 0;
  bool valid = false;
  bool anyOk = false;
  uint32_t updatedMs = 0;
  bool speedValid = false;
  uint32_t speedUpdatedMs = 0;
};

void modbusTaskBegin();
void mbInvalidateMotion();
uint32_t mbMotionEpoch();
bool mbRunIfCurrent(uint32_t expectedEpoch, TickType_t timeout = pdMS_TO_TICKS(300));
bool mbCall(MbOp op, uint16_t arg = 0, TickType_t timeout = pdMS_TO_TICKS(300));
bool mbCallUrgent(MbOp op, uint16_t arg = 0, TickType_t timeout = pdMS_TO_TICKS(300));
bool mbCallDriverPacket(const uint8_t request[DRIVER_PACKET_REQUEST_SIZE], DriverPacketResult &result,
                       TickType_t timeout = pdMS_TO_TICKS(1000));

Telemetry mbTelemetry();

struct OvercurrentAlarm {
  uint16_t amps = 0;
  bool valid = false;
};
OvercurrentAlarm mbOvercurrentAlarm();
