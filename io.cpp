#include "io.h"
#include "globals.h"
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
  PulseSpeedSnapshot pulseSpeed = getPulseSpeedSnapshot();

  doc["version"] = config.version;
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
  doc["motor_rpm_pulse"] = pulseSpeed.valid ? pulseSpeed.rpm : 0.0f;
  doc["motor_rpm_pulse_valid"] = pulseSpeed.valid;
  doc["speed_pulse_hz"] = pulseSpeed.valid ? pulseSpeed.frequencyHz : 0.0f;
  doc["speed_pulse_age_ms"] = pulseSpeed.ageMs;
  doc["speed_pulse_jitter_us"] = pulseSpeed.jitterUs;
  doc["motor_fault"] = t.fault;
  doc["telemetry_valid"] = t.valid;
  doc["telemetry_age_ms"] = t.updatedMs ? (millis() - t.updatedMs) : 0;

  RampState r = rampGetState();
  JsonObject ramp = doc["ramp"].to<JsonObject>();
  ramp["active"] = r.active;
  ramp["segment"] = r.segmentIndex;
  ramp["segments"] = r.segmentCount;
  ramp["setpoint"] = r.currentSetpoint;
  ramp["elapsed_ms"] = r.elapsedMs;
  ramp["total_ms"] = r.totalMs;

  String output;
  serializeJson(doc, output);
  return output;
}
