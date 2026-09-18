#include "ramp.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "hardware.h"
#include "motor.h"
#include "ota.h"
#include "rtos.h"

struct RampSegment {
  uint16_t target;
  uint32_t durationMs;
};

struct RampProfile {
  uint16_t startValue;
  uint8_t count;
  uint32_t totalMs;
  RampSegment segments[RAMP_MAX_SEGMENTS];
};

static SemaphoreHandle_t rampMutex = nullptr;
static RampProfile profile{};
static volatile bool active = false;
static uint32_t startMs = 0;

static bool applyRampSetpoint(uint16_t setpoint) {
  if (setpoint == 0) {
    if (!isMotorRunning()) return true;
    return stopMotor();
  }
  if (config.controlMode == ControlMode::ClosedLoop &&
      setpoint < CLOSED_LOOP_RPM_MIN && !isMotorRunning())
    return true;
  if (isMotorRunning()) return motorSetRuntimeSetpoint(setpoint);
  if (!motorConfigureForRun(ACCEL_MS_MIN)) return false;
  if (!motorSetRuntimeSetpoint(setpoint)) return false;
  if (!motorRun(true)) return false;
  applyMotorRunState(true);
  return true;
}

static void lock() {
  if (rampMutex) xSemaphoreTake(rampMutex, portMAX_DELAY);
}

static void unlock() {
  if (rampMutex) xSemaphoreGive(rampMutex);
}

static bool validSetpoint(unsigned long value) {
  if (value == 0) return true;
  if (config.controlMode == ControlMode::ClosedLoop)
    return value >= CLOSED_LOOP_RPM_MIN && value <= CLOSED_LOOP_RPM_MAX;
  return value <= OPEN_LOOP_RAW_MAX;
}

static bool parseUnsigned(const char *start, char **end, unsigned long &value) {
  if (!start || *start < '0' || *start > '9') return false;

  errno = 0;
  value = strtoul(start, end, 10);
  return errno != ERANGE && *end != start;
}

static bool parseProfile(const char *spec, RampProfile &out) {
  if (!spec || !*spec) return false;

  out = {};
  const char *p = spec;
  const char* bar = strchr(spec, '|');

  if (bar) {
    char *end = nullptr;
    unsigned long startValue = 0;
    if (!parseUnsigned(p, &end, startValue) || end != bar || !validSetpoint(startValue))
      return false;
    out.startValue = static_cast<uint16_t>(startValue);
    p = bar + 1;
  }

  while (*p) {
    if (out.count >= RAMP_MAX_SEGMENTS) return false;

    char *end = nullptr;
    unsigned long target = 0;
    if (!parseUnsigned(p, &end, target) || !validSetpoint(target)) return false;
    p = end;
    if (*p++ != ',') return false;

    unsigned long duration = 0;
    if (!parseUnsigned(p, &end, duration) || duration > UINT32_MAX) return false;
    p = end;
    if (*p != '\0' && *p != ';') return false;
    if (UINT32_MAX - out.totalMs < duration) return false;

    out.segments[out.count++] = {
      static_cast<uint16_t>(target),
      static_cast<uint32_t>(duration)
    };
    out.totalMs += static_cast<uint32_t>(duration);
    if (*p == ';') {
      ++p;
      if (!*p) return false;
    }
  }

  return out.count > 0;
}

static uint16_t setpointAt(uint32_t elapsed) {
  uint32_t accumulatedMs = 0;
  uint16_t segmentStart = profile.startValue;

  for (uint8_t i = 0; i < profile.count; ++i) {
    uint32_t durationMs = profile.segments[i].durationMs;
    uint16_t target = profile.segments[i].target;

    if (durationMs == 0) {
      segmentStart = target;
      continue;
    }
    if (elapsed < accumulatedMs + durationMs) {
      float fraction = static_cast<float>(elapsed - accumulatedMs) /
                       static_cast<float>(durationMs);
      float value = static_cast<float>(segmentStart) +
                    (static_cast<float>(target) - segmentStart) * fraction;
      return static_cast<uint16_t>(value + 0.5f);
    }
    accumulatedMs += durationMs;
    segmentStart = target;
  }
  return segmentStart;
}

static void rampTaskFn(void *) {
  for (;;) {
    if (!active) {
      vTaskDelay(pdMS_TO_TICKS(RAMP_TICK_MS));
      continue;
    }
    if (isEmergencyLatched() || physicalSafetyInputsActive()) {
      active = false;
      continue;
    }

    lock();
    uint32_t elapsed = millis() - startMs;
    bool done = elapsed >= profile.totalMs;
    if (done) elapsed = profile.totalMs;
    uint16_t setpoint = setpointAt(elapsed);
    uint16_t finalTarget = profile.segments[profile.count - 1].target;
    unlock();

    if (!applyRampSetpoint(setpoint)) {
      active = false;
      setEmergencyLatched(true);
      continue;
    }
    if (done) {
      active = false;
      if (finalTarget == 0 && isMotorRunning() && !stopMotor())
        setEmergencyLatched(true);
    }

    vTaskDelay(pdMS_TO_TICKS(RAMP_TICK_MS));
  }
}

bool rampStartProfile(const char *spec, const char **errField) {
  if (errField) *errField = "ramp";
  if (isEmergencyLatched() || physicalSafetyInputsActive() || otaInProgress()) return false;

  RampProfile parsed;
  if (!parseProfile(spec, parsed)) return false;

  lock();
  profile = parsed;
  unlock();

  if (!applyRampSetpoint(parsed.startValue)) return false;
  startMs = millis();
  active = true;
  if (errField) *errField = nullptr;
  return true;
}

void rampStop() {
  active = false;
}

RampState rampGetState() {
  return { active };
}

void rampBegin() {
  rampMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(rampTaskFn, "ramp", 4096, nullptr, PRIO_RAMP, nullptr, 1);
}
