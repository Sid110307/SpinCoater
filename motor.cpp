#include "motor.h"

#include "config.h"
#include "hardware.h"
#include "modbus.h"

static uint16_t clampClosedLoopRpm(uint16_t rpm) {
  if (rpm < 200)
    return 200;
  if (rpm > 4000)
    return 4000;
  return rpm;
}

static uint16_t clampOpenLoopCommandRaw(uint16_t raw) {
  if (raw > 1000)
    return 1000;
  return raw;
}

static uint16_t clampAccelerationMs(uint16_t ms) {
  if (ms < 300)
    return 300;
  if (ms > 15000)
    return 15000;
  return ms;
}

static bool applyMotorProfile(bool running) {
  if (!modbusUseCommunicationControl())
    return false;
  if (!modbusSetControlMode(config.controlMode == ControlMode::ClosedLoop))
    return false;
  if (!modbusSetDirection(config.direction == MotorDirection::Clockwise))
    return false;
  if (!modbusSetAccelTimeMs(clampAccelerationMs(config.accelerationMs)))
    return false;

  if (config.controlMode == ControlMode::ClosedLoop) {
    if (!modbusSetClosedLoopRpm(clampClosedLoopRpm(config.targetRpm)))
      return false;
  } else {
    if (!modbusSetOpenLoopSpeedPercent(
          clampOpenLoopCommandRaw(config.openLoopCommandRaw)))
      return false;
  }

  if (!modbusSetBrake(false))
    return false;
  if (!modbusSetRun(running))
    return false;

  return true;
}

bool startMotor() {
  if (isEmergencyLatched())
    return false;
  if (!applyMotorProfile(true))
    return false;

  applyMotorRunState(true);
  return true;
}

void stopMotor() {
  modbusSetRun(false);
  modbusSetBrake(false);
  applyMotorRunState(false);
}

void emergencyStopMotor() {
  modbusSetBrake(true);
  modbusSetRun(false);
  applyMotorRunState(false);
}

bool changeControlMode(ControlMode controlMode) {
  if (config.controlMode == controlMode)
    return true;

  config.controlMode = controlMode;
  saveConfig();

  if (isMotorRunning()) {
    if (!modbusSetControlMode(controlMode == ControlMode::ClosedLoop))
      return false;
    if (controlMode == ControlMode::ClosedLoop) {
      if (!modbusSetClosedLoopRpm(clampClosedLoopRpm(config.targetRpm)))
        return false;
    } else {
      if (!modbusSetOpenLoopSpeedPercent(
            clampOpenLoopCommandRaw(config.openLoopCommandRaw)))
        return false;
    }
  }

  return true;
}

bool changeTargetRpm(uint16_t rpm) {
  if (rpm < 200 || rpm > 4000)
    return false;
  if (config.targetRpm == rpm)
    return true;

  config.targetRpm = rpm;
  saveConfig();

  if (isMotorRunning() && config.controlMode == ControlMode::ClosedLoop) {
    if (!modbusSetClosedLoopRpm(clampClosedLoopRpm(rpm)))
      return false;
  }

  return true;
}

bool changeOpenLoopCommandRaw(uint16_t raw) {
  if (raw > 1000)
    return false;
  if (config.openLoopCommandRaw == raw)
    return true;

  config.openLoopCommandRaw = raw;
  saveConfig();

  if (isMotorRunning() && config.controlMode == ControlMode::OpenLoop) {
    if (!modbusSetOpenLoopSpeedPercent(clampOpenLoopCommandRaw(raw)))
      return false;
  }

  return true;
}

bool changeAccelerationMs(uint16_t accelerationMs) {
  if (accelerationMs < 300 || accelerationMs > 15000)
    return false;
  if (config.accelerationMs == accelerationMs)
    return true;

  config.accelerationMs = accelerationMs;
  saveConfig();

  if (isMotorRunning()) {
    if (!modbusSetAccelTimeMs(clampAccelerationMs(accelerationMs)))
      return false;
  }

  return true;
}

bool changeDirection(MotorDirection direction) {
  if (config.direction != direction) {
    config.direction = direction;
    saveConfig();
  }

  if (isMotorRunning()) {
    if (!modbusSetDirection(direction == MotorDirection::Clockwise))
      return false;
  }

  setMotorDirection(direction);

  return true;
}
