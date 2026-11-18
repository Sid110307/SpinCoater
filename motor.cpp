#include "motor.h"

#include "hardware.h"
#include "modbus_task.h"
#include "ota.h"
#include "safety.h"

static SemaphoreHandle_t motorMutex = nullptr;
static volatile bool brakeStopPending = false;
static uint32_t brakeStopSinceMs = 0;

static void motorLock() {
  if (motorMutex) xSemaphoreTakeRecursive(motorMutex, portMAX_DELAY);
}

static void motorUnlock() {
  if (motorMutex) xSemaphoreGiveRecursive(motorMutex);
}

void motorBegin() {
  motorMutex = xSemaphoreCreateRecursiveMutex();
}

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

static bool configureForRun(uint16_t accelMs) {
  if (!mbCall(MbOp::CommSource)) return false;
  if (!mbCall(MbOp::ControlMode, config.controlMode == ControlMode::ClosedLoop))
    return false;
  if (!mbCall(MbOp::Direction, config.direction == MotorDirection::Clockwise))
    return false;
  if (!mbCall(MbOp::AccelMs, clampAccelerationMs(accelMs))) return false;
  if (!mbCall(MbOp::PolePairs, config.polePairs)) return false;
  if (!mbCall(MbOp::PwmMax, config.pwmMaxPercent)) return false;
  if (config.currentLimitAmps != 0 && !mbCall(MbOp::CurrentLimit, config.currentLimitAmps))
    return false;
  if (!mbCall(MbOp::Brake, 0)) return false;
  return true;
}

bool motorConfigureForRun(uint16_t accelMs) {
  motorLock();
  brakeStopPending = false;
  bool ok = configureForRun(accelMs);
  motorUnlock();
  return ok;
}

bool motorRun(bool running) {
  if (running && physicalSafetyInputsActive()) return false;
  return mbCall(MbOp::Run, running ? 1 : 0);
}

bool startMotor() {
  if (isEmergencyLatched() || physicalSafetyInputsActive() || otaInProgress()) return false;
  Telemetry telemetry = mbTelemetry();
  if (telemetry.valid && telemetry.fault != 0) return false;
  motorLock();
  bool ok = motorConfigureForRun(config.accelerationMs);

  uint16_t setpoint = config.controlMode == ControlMode::ClosedLoop
                        ? config.targetRpm
                        : config.openLoopCommandRaw;
  ok = ok && motorSetRuntimeSetpoint(setpoint) && motorRun(true);
  if (ok) applyMotorRunState(true);
  motorUnlock();
  if (ok) safetyClearTrip();
  return ok;
}

bool stopMotor() {
  motorLock();
  bool ok = true;
  if (mbCall(MbOp::Brake, 1)) {
    brakeStopPending = true;
    brakeStopSinceMs = millis();
  } else {
    brakeStopPending = false;
    ok = mbCall(MbOp::Run, 0);
  }
  if (ok) applyMotorRunState(false);
  motorUnlock();
  return ok;
}

void motorServiceBrakeStop(float rpm) {
  if (!brakeStopPending) return;
  motorLock();
  if (brakeStopPending && isEmergencyLatched()) {
    brakeStopPending = false;
  } else if (brakeStopPending &&
             (rpm < SPINNING_RPM || millis() - brakeStopSinceMs > BRAKE_STOP_TIMEOUT_MS)) {
    if (mbCall(MbOp::Run, 0)) {
      mbCall(MbOp::Brake, 0);
      brakeStopPending = false;
    }
  }
  motorUnlock();
}

bool motorBrakeStopPending() { return brakeStopPending; }

void emergencyStopMotor() {
  bool braked = false;
  for (int attempt = 0; attempt < 4 && !braked; ++attempt)
    braked = mbCallUrgent(MbOp::Brake, 1, pdMS_TO_TICKS(120));
  if (!braked)
    for (int attempt = 0; attempt < 4; ++attempt)
      if (mbCallUrgent(MbOp::Run, 0, pdMS_TO_TICKS(120))) break;
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

bool changePolePairs(uint8_t pairs) {
  if (pairs < POLE_PAIRS_MIN || pairs > POLE_PAIRS_MAX) return false;
  if (config.polePairs == pairs) return true;
  if (isMotorRunning() && !stopMotor()) return false;

  config.polePairs = pairs;
  saveConfig();
  return true;
}

bool changeCurrentLimitAmps(uint8_t amps) {
  if (amps != 0 && (amps < CURRENT_LIMIT_MIN_A || amps > CURRENT_LIMIT_MAX_A)) return false;
  if (config.currentLimitAmps == amps) return true;
  if (isMotorRunning() && amps != 0)
    if (!mbCall(MbOp::CurrentLimit, amps)) return false;

  config.currentLimitAmps = amps;
  saveConfig();
  return true;
}

bool changePwmMaxPercent(uint8_t percent) {
  if (percent < PWM_MAX_MIN || percent > PWM_MAX_MAX) return false;
  if (config.pwmMaxPercent == percent) return true;
  if (isMotorRunning())
    if (!mbCall(MbOp::PwmMax, percent)) return false;

  config.pwmMaxPercent = percent;
  saveConfig();
  return true;
}
