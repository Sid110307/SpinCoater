#include "ramp.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cstdlib>
#include <cstring>

#include "hardware.h"
#include "modbus_task.h"
#include "motor.h"
#include "rtos.h"

static SemaphoreHandle_t rampMutex = nullptr;

static RampProfile profile;
static bool profileLoaded = false;

static RampProfile builder;
static bool builderPending = false;

static volatile bool active = false;
static uint32_t startMs = 0;
static uint16_t currentSetpoint = 0;
static uint8_t currentSegment = 0;

static void lock() {
  if (rampMutex) xSemaphoreTake(rampMutex, portMAX_DELAY);
}
static void unlock() {
  if (rampMutex) xSemaphoreGive(rampMutex);
}

static uint16_t setpointAt(uint32_t elapsed, uint8_t &segOut) {
  uint32_t acc = 0;
  uint16_t segStart = profile.startValue;

  for (uint8_t i = 0; i < profile.count; ++i) {
    uint32_t dur = profile.segments[i].durationMs;
    uint16_t target = profile.segments[i].target;

    if (dur == 0) {
      segStart = target;
      continue;
    }
    if (elapsed < acc + dur) {
      segOut = i;
      float frac = static_cast<float>(elapsed - acc) / static_cast<float>(dur);
      float value =
        static_cast<float>(segStart) + (static_cast<float>(target) - static_cast<float>(segStart)) * frac;
      if (value < 0.0f) value = 0.0f;
      return static_cast<uint16_t>(value + 0.5f);
    }
    acc += dur;
    segStart = target;
  }
  segOut = profile.count ? profile.count - 1 : 0;
  return segStart;
}

static void rampTaskFn(void *) {
  for (;;) {
    if (!active) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (mbIsEmergency() || isEmergencyLatched() || physicalSafetyInputsActive()) {
      active = false;
      continue;
    }

    lock();
    uint32_t total = profile.totalMs;
    uint32_t elapsed = millis() - startMs;

    bool done = elapsed >= total;
    if (done) elapsed = total;

    uint8_t seg = 0;
    uint16_t sp = setpointAt(elapsed, seg);
    uint16_t finalTarget = profile.count ? profile.segments[profile.count - 1].target : 0;
    unlock();

    motorSetRuntimeSetpoint(sp);
    currentSetpoint = sp;
    currentSegment = seg;

    if (done) {
      active = false;
      if (finalTarget == 0) stopMotor();
    }

    vTaskDelay(pdMS_TO_TICKS(RAMP_TICK_MS));
  }
}

bool rampParse(const char *spec, RampProfile &out, const char **errField) {
  if (errField) *errField = "ramp";
  if (!spec) return false;

  out.count = 0;
  out.startValue = 0;
  out.totalMs = 0;

  const char *p = spec;
  const char *bar = strchr(spec, '|');
  if (bar) {
    char *end = nullptr;
    long s = strtol(spec, &end, 10);

    if (end == spec || s < 0) return false;
    out.startValue = static_cast<uint16_t>(s);
    p = bar + 1;
  }

  while (*p) {
    while (*p == ' ' || *p == ';') ++p;
    if (!*p) break;

    char *end = nullptr;
    long target = strtol(p, &end, 10);
    if (end == p || target < 0) return false;
    p = end;
    while (*p == ' ') ++p;
    if (*p != ',') return false;
    ++p;

    long dur = strtol(p, &end, 10);
    if (end == p || dur < 0) return false;
    p = end;

    if (out.count >= RAMP_MAX_SEGMENTS) return false;
    out.segments[out.count].target = static_cast<uint16_t>(target);
    out.segments[out.count].durationMs = static_cast<uint32_t>(dur);
    out.totalMs += static_cast<uint32_t>(dur);
    out.count++;

    while (*p == ' ') ++p;
    if (*p && *p != ';') return false;
  }

  if (out.count == 0) return false;
  if (errField) *errField = nullptr;
  return true;
}

bool rampLoad(const RampProfile &p) {
  lock();
  profile = p;
  profileLoaded = true;
  unlock();

  return true;
}

void rampBuilderReset(uint16_t startValue) {
  lock();
  builder.count = 0;
  builder.startValue = startValue;
  builder.totalMs = 0;
  builderPending = true;
  unlock();
}

void rampBuilderSetStart(uint16_t startValue) {
  lock();
  builder.startValue = startValue;
  builderPending = true;
  unlock();
}

bool rampBuilderAdd(uint16_t target, uint32_t durationMs) {
  lock();
  bool ok = false;
  if (builder.count < RAMP_MAX_SEGMENTS) {
    builder.segments[builder.count].target = target;
    builder.segments[builder.count].durationMs = durationMs;
    builder.totalMs += durationMs;
    builder.count++;
    builderPending = true;
    ok = true;
  }
  unlock();
  return ok;
}

bool rampStart() {
  if (isEmergencyLatched() || physicalSafetyInputsActive()) return false;
  lock();

  if (builderPending && builder.count > 0) {
    profile = builder;
    profileLoaded = true;
    builderPending = false;
  }
  bool ok = profileLoaded && profile.count > 0;
  uint16_t start = profile.startValue;
  unlock();
  if (!ok) return false;

  if (!motorConfigureForRun(ACCEL_MS_MIN)) return false;
  if (!motorSetRuntimeSetpoint(start)) return false;
  if (!motorRun(true)) return false;
  applyMotorRunState(true);

  startMs = millis();
  currentSegment = 0;
  currentSetpoint = start;
  active = true;

  return true;
}

bool rampParseLoadStart(const char *spec, const char **errField) {
  RampProfile p;
  if (!rampParse(spec, p, errField)) return false;

  rampLoad(p);
  if (!rampStart()) {
    if (errField) *errField = "ramp";
    return false;
  }
  return true;
}

void rampStop() {
  active = false;
}

RampState rampGetState() {
  RampState s;

  lock();
  s.active = active;
  s.segmentIndex = currentSegment;
  s.segmentCount = profile.count;
  s.currentSetpoint = currentSetpoint;
  s.totalMs = profile.totalMs;
  s.elapsedMs = active ? (millis() - startMs) : 0;

  if (s.elapsedMs > s.totalMs) s.elapsedMs = s.totalMs;
  unlock();

  return s;
}

void rampBegin() {
  rampMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(rampTaskFn, "ramp", 4096, nullptr, PRIO_RAMP, nullptr, 1);
}
