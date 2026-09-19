#pragma once

#include <freertos/FreeRTOS.h>

enum class MbOp : uint8_t {
  CommSource,
  ControlMode,      // 0 open loop, 1 closed loop
  ClosedLoopRpm,    // rpm, 200..4000
  OpenLoopPercent,  // raw 100..1000 (10% .. 100%)
  AccelMs,          // 300..15000
  Run,              // 0 stop, 1 run
  Direction,        // 0 CW, 1 CCW
  Brake,            // 0 release, 1 brake
  ClearFault,
  PolePairs,        // 1..10
  CurrentLimit,     // 3..35 A
  PwmMax,           // 10..100 %
  RefreshTelemetry,
  RefreshOvercurrentAlarm
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

struct OvercurrentAlarm {
  uint16_t amps = 0;
  bool valid = false;
};
OvercurrentAlarm mbOvercurrentAlarm();
