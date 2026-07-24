#include "motor.h"

#include "config.h"
#include "hardware.h"
#include "modbus.h"

static bool preferClosedLoop = false;

static uint16_t clampClosedLoopRpm(int rpm) {
  if (rpm < 200) return 200;
  if (rpm > 4000) return 4000;
  return static_cast<uint16_t>(rpm);
}

static uint8_t clampOpenLoopPercent(float percent) {
  if (percent < 10.0f) return 100;
  if (percent > 100.0f) return 1000;
  return static_cast<uint8_t>(percent * 10);
}

static uint16_t clampAccelTime(float ms) {
  if (ms < 300.0f) return 300;
  if (ms > 15000.0f) return 15000;
  return static_cast<uint16_t>(ms);
}

static bool applyMotorProfile(bool running) {
  if (!modbusUseCommunicationControl()) return false;
  if (!modbusSetControlMode(preferClosedLoop)) return false;
  if (!modbusSetDirection(getMotorDirection() == MotorDirection::Clockwise)) return false;
  if (!modbusSetAccelTimeMs(clampAccelTime(config.duration))) return false;

  if (preferClosedLoop) {
    if (!modbusSetClosedLoopRpm(clampClosedLoopRpm(config.frequency))) return false;
  } else {
    if (!modbusSetOpenLoopSpeedPercent(clampOpenLoopPercent(config.dutyCycle))) return false;
  }

  if (!modbusSetBrake(false)) return false;
  if (!modbusSetRun(running)) return false;

  return true;
}

bool startMotor() {
  if (isEmergencyLatched() || isAlarmLatched()) return false;
  if (!applyMotorProfile(true)) return false;

  applyMotorRunState(true);
  return true;
}

void stopMotor() {
  modbusSetRun(false);
  modbusSetBrake(false);
  applyMotorRunState(false);
}

bool changeFreq(int freq) {
  if (config.frequency == freq) return true;
  if (freq <= 0 || freq > MAX_FREQ) return false;

  config.frequency = freq;
  preferClosedLoop = true;
  saveConfig();

  if (isMotorRunning()) {
    if (!modbusSetControlMode(true)) return false;
    if (!modbusSetClosedLoopRpm(clampClosedLoopRpm(freq))) return false;
  }

  return true;
}

bool changeDuty(float duty) {
  if (config.dutyCycle == duty) return true;
  if (duty < 0 || duty > 100) return false;

  config.dutyCycle = duty;
  preferClosedLoop = false;
  saveConfig();

  if (isMotorRunning()) {
    if (!modbusSetControlMode(false)) return false;
    if (!modbusSetOpenLoopSpeedPercent(clampOpenLoopPercent(duty))) return false;
  }

  return true;
}

bool changeLen(float len) {
  if (config.length == len) return true;
  if (len < 0) return false;

  config.length = len;
  saveConfig();

  return true;
}

bool changeDur(float dur) {
  if (config.duration == dur) return true;
  if (dur < 0) return false;

  config.duration = dur;
  saveConfig();

  if (isMotorRunning()) {
    if (!modbusSetAccelTimeMs(clampAccelTime(dur))) return false;
  }

  return true;
}

bool changePin(int pin) {
  if (config.pwmPin == pin) return true;
  if (!isValidMotorPwmPin(pin)) return false;

  config.pwmPin = pin;
  saveConfig();

  return true;
}

bool addPass(float rpm, float accel, float delay) {
  if (config.numPasses >= MAX_PASSES) return false;
  if (rpm < 0 || accel < 0 || delay < 0) return false;

  config.passes[config.numPasses].rpm = rpm;
  config.passes[config.numPasses].accel = accel;
  config.passes[config.numPasses].delay = delay;
  config.numPasses++;
  saveConfig();

  return true;
}