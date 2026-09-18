#include "io.h"

#include <ArduinoJson.h>

#include "config.h"
#include "hardware.h"
#include "modbus_task.h"
#include "ramp.h"

static const char *controlModeToString(ControlMode controlMode) {
  switch (controlMode) {
    case ControlMode::OpenLoop: return "open_loop";
    case ControlMode::ClosedLoop: return "closed_loop";
    default: return "unknown";
  }
}

String getJSONString() {
  JsonDocument doc;
  Telemetry t = mbTelemetry();

  doc["version"] = config.version;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["modbus_address"] = config.modbusAddress;
  doc["control_mode"] = controlModeToString(config.controlMode);
  doc["direction"] = config.direction == MotorDirection::Clockwise ? "cw" : "ccw";
  doc["target_rpm"] = config.targetRpm;
  doc["open_loop_raw"] = config.openLoopCommandRaw;
  doc["accel_ms"] = config.accelerationMs;
  doc["status"] = motorStatusToString(getMotorStatus());
  doc["running"] = isMotorRunning();
  doc["emergency"] = isEmergencyLatched();

  doc["motor_voltage"] = t.voltage;
  doc["motor_current"] = static_cast<float>(t.current) / 100.0f;
  doc["motor_power"] = static_cast<float>(t.power) / 10.0f;
  doc["motor_speed"] = t.speed;
  doc["motor_fault"] = t.fault;
  doc["telemetry_valid"] = t.valid;
  doc["telemetry_age_ms"] = t.updatedMs ? (millis() - t.updatedMs) : 0;

  RampState r = rampGetState();
  JsonObject ramp = doc["ramp"].to<JsonObject>();
  ramp["active"] = r.active;

  String output;
  serializeJson(doc, output);
  return output;
}
