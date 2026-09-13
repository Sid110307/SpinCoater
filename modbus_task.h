#pragma once

#include <freertos/FreeRTOS.h>

enum class MbOp : uint8_t {
  CommSource,
  ControlMode,      // 0 open loop, 1 closed loop
  ClosedLoopRpm,    // rpm
  OpenLoopPercent,  // raw 0..1000 (10% .. 100%)
  AccelMs,          // 300..15000
  Run,              // 0 stop, 1 run
  Direction,        // 1 CW, 0 CCW
  Brake,            // 0 release, 1 brake
  ClearFault,
  RefreshTelemetry
};

struct Telemetry {
  uint16_t voltage = 0;  // V
  uint16_t current = 0;  // 10 mA units
  uint16_t power = 0;    // 0.1 W units
  uint16_t speed = 0;    // rpm
  uint16_t fault = 0;    // fault code
  bool valid = false;
  uint32_t updatedMs = 0;
};

void modbusTaskBegin();
bool mbCall(MbOp op, uint16_t arg = 0, TickType_t timeout = pdMS_TO_TICKS(300));
bool mbCallUrgent(MbOp op, uint16_t arg = 0, TickType_t timeout = pdMS_TO_TICKS(300));

Telemetry mbTelemetry();
