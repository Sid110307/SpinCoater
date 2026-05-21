#include "motor.h"
#include "config.h"
#include "hardware.h"

void setupPWM() {
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, config.pwmPin);

  mcpwm_config_t pwm_config;
  pwm_config.frequency = config.frequency;  // Frequency in Hz
  pwm_config.cmpr_a = config.dutyCycle;     // Initial duty cycle
  pwm_config.cmpr_b = 0;                    // Not used for Generator A
  pwm_config.counter_mode = MCPWM_UP_COUNTER;
  pwm_config.duty_mode = MCPWM_DUTY_MODE_0;  // Active High

  mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
  mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, 0);
  mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0);
}

bool startMotor() {
  if (isEmergencyLatched() || isAlarmLatched()) return false;

  applyMotorRunState(true);
  mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, config.dutyCycle);
  mcpwm_start(MCPWM_UNIT_0, MCPWM_TIMER_0);

  return true;
}

void stopMotor() {
  mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, 0);
  mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0);
  applyMotorRunState(false);
}

bool changeFreq(int freq) {
  if (config.frequency == freq) return true;
  if (freq <= 0 || freq > MAX_FREQ) return false;

  config.frequency = freq;
  mcpwm_set_frequency(MCPWM_UNIT_0, MCPWM_TIMER_0, freq);
  // Duty cycle also resets when frequency changes
  mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, config.dutyCycle);
  saveConfig();

  return true;
}

bool changeDuty(float duty) {
  if (config.dutyCycle == duty) return true;
  if (duty < 0 || duty > 100) return false;

  config.dutyCycle = duty;
  mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, duty);
  saveConfig();

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

  return true;
}

bool changePin(int pin) {
  if (config.pwmPin == pin) return true;
  if (!isValidMotorPwmPin(pin)) return false;

  const bool wasRunning = isMotorRunning();
  if (wasRunning) stopMotor();

  config.pwmPin = pin;
  setupPWM();
  saveConfig();

  if (wasRunning && !isEmergencyLatched() && !isAlarmLatched()) startMotor();
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