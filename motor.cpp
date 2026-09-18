#include "motor.h"

#include "hardware.h"
#include "modbus_task.h"

static uint16_t clampClosedLoopRpm(uint16_t rpm) {
  if (rpm < CLOSED_LOOP_RPM_MIN) return CLOSED_LOOP_RPM_MIN;
  if (rpm > CLOSED_LOOP_RPM_MAX) return CLOSED_LOOP_RPM_MAX;
  return rpm;
}

static uint16_t clampOpenLoopCommandRaw(uint16_t raw) {
  if (raw < OPEN_LOOP_RAW_MIN) return OPEN_LOOP_RAW_MIN;
  if (raw > OPEN_LOOP_RAW_MAX) return OPEN_LOOP_RAW_MAX;
  return raw;
}

static uint16_t clampAccelerationMs(uint16_t ms) {
  if (ms < ACCEL_MS_MIN) return ACCEL_MS_MIN;
  if (ms > ACCEL_MS_MAX) return ACCEL_MS_MAX;
  return ms;
}

bool motorSetRuntimeSetpoint(uint16_t value) {
  if (config.controlMode == ControlMode::ClosedLoop)
    return mbCall(MbOp::ClosedLoopRpm, clampClosedLoopRpm(value));
  return mbCall(MbOp::OpenLoopPercent, clampOpenLoopCommandRaw(value));
}

bool motorConfigureForRun(uint16_t accelMs) {
  if (!mbCall(MbOp::CommSource)) return false;
  if (!mbCall(MbOp::ControlMode, config.controlMode == ControlMode::ClosedLoop))
    return false;
  if (!mbCall(MbOp::Direction, config.direction == MotorDirection::Clockwise))
    return false;
  if (!mbCall(MbOp::AccelMs, clampAccelerationMs(accelMs))) return false;
  if (!mbCall(MbOp::Brake, 0)) return false;
  return true;
}

bool motorRun(bool running) {
  if (running && physicalSafetyInputsActive()) return false;
  return mbCall(MbOp::Run, running ? 1 : 0);
}

bool startMotor() {
  if (isEmergencyLatched() || physicalSafetyInputsActive()) return false;
  Telemetry telemetry = mbTelemetry();
  if (telemetry.valid && telemetry.fault != 0) return false;
  if (!motorConfigureForRun(config.accelerationMs)) return false;

  uint16_t setpoint = config.controlMode == ControlMode::ClosedLoop
                        ? config.targetRpm
                        : config.openLoopCommandRaw;
  if (!motorSetRuntimeSetpoint(setpoint)) return false;
  if (!motorRun(true)) return false;

  applyMotorRunState(true);
  return true;
}

bool stopMotor() {
  if (!mbCall(MbOp::Run, 0)) return false;
  if (!mbCall(MbOp::Brake, 0)) return false;
  applyMotorRunState(false);
  return true;
}

void emergencyStopMotor() {
  mbCallUrgent(MbOp::Brake, 1);
  mbCallUrgent(MbOp::Run, 0);
  applyMotorRunState(false);
}

bool changeControlMode(ControlMode controlMode) {
  if (config.controlMode == controlMode) return true;
  if (isMotorRunning()) {
    if (!mbCall(MbOp::ControlMode, controlMode == ControlMode::ClosedLoop))
      return false;
    uint16_t setpoint = controlMode == ControlMode::ClosedLoop
                          ? config.targetRpm
                          : config.openLoopCommandRaw;
    MbOp op = controlMode == ControlMode::ClosedLoop
                ? MbOp::ClosedLoopRpm
                : MbOp::OpenLoopPercent;
    if (!mbCall(op, setpoint)) {
      mbCall(MbOp::ControlMode, config.controlMode == ControlMode::ClosedLoop);
      return false;
    }
  }
  config.controlMode = controlMode;
  saveConfig();
  return true;
}

bool changeTargetRpm(uint16_t rpm) {
  if (rpm < CLOSED_LOOP_RPM_MIN || rpm > CLOSED_LOOP_RPM_MAX) return false;
  if (config.targetRpm == rpm) return true;
  if (isMotorRunning() && config.controlMode == ControlMode::ClosedLoop)
    if (!mbCall(MbOp::ClosedLoopRpm, rpm)) return false;
  config.targetRpm = rpm;
  saveConfig();
  return true;
}

bool changeOpenLoopCommandRaw(uint16_t raw) {
  if (raw < OPEN_LOOP_RAW_MIN || raw > OPEN_LOOP_RAW_MAX) return false;
  if (config.openLoopCommandRaw == raw) return true;
  if (isMotorRunning() && config.controlMode == ControlMode::OpenLoop)
    if (!mbCall(MbOp::OpenLoopPercent, raw)) return false;
  config.openLoopCommandRaw = raw;
  saveConfig();
  return true;
}

bool changeAccelerationMs(uint16_t accelerationMs) {
  if (accelerationMs < ACCEL_MS_MIN || accelerationMs > ACCEL_MS_MAX)
    return false;
  if (config.accelerationMs == accelerationMs) return true;
  if (isMotorRunning())
    if (!mbCall(MbOp::AccelMs, accelerationMs)) return false;
  config.accelerationMs = accelerationMs;
  saveConfig();
  return true;
}

bool changeDirection(MotorDirection direction) {
  if (isMotorRunning() && !stopMotor()) return false;
  if (config.direction != direction) {
    config.direction = direction;
    saveConfig();
  }
  setMotorDirection(direction);
  return true;
}
