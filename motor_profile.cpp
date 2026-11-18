#include "motor_profile.h"

#include <freertos/semphr.h>
#include <freertos/task.h>

#include "hardware.h"
#include "modbus_task.h"
#include "rtos.h"

struct DerivativePoint {
  uint32_t elapsedUs;
  float rpm;
};

static SemaphoreHandle_t profileMutex = nullptr;
static MotorProfileSummary profileSummary{};
static MotorProfileSample sampleBuffer[PROFILE_BUFFER_SAMPLES]{};
static uint16_t sampleHead = 0;
static uint16_t sampleTail = 0;
static uint16_t bufferedSamples = 0;
static uint32_t profileStartUs = 0;
static uint32_t lastObservedPulseCount = 0;
static bool lastSpeedValid = false;

static DerivativePoint derivativePoints[PROFILE_ACCEL_WINDOW_SAMPLES]{};
static uint8_t derivativeHead = 0;
static uint8_t derivativeCount = 0;

static void lockProfile() {
  if (profileMutex) xSemaphoreTake(profileMutex, portMAX_DELAY);
}

static void unlockProfile() {
  if (profileMutex) xSemaphoreGive(profileMutex);
}

static void clearCaptureLocked() {
  bool active = profileSummary.active;
  uint32_t sessionId = profileSummary.sessionId;
  profileSummary = {};
  profileSummary.active = active;
  profileSummary.sessionId = sessionId;
  sampleHead = 0;
  sampleTail = 0;
  bufferedSamples = 0;
  derivativeHead = 0;
  derivativeCount = 0;
}

static float accelerationForLocked(uint32_t elapsedUs, float rpm) {
  float acceleration = 0.0f;
  if (derivativeCount > 0) {
    uint8_t oldest = derivativeCount == PROFILE_ACCEL_WINDOW_SAMPLES ? derivativeHead : 0;
    const DerivativePoint &reference = derivativePoints[oldest];
    uint32_t deltaUs = elapsedUs - reference.elapsedUs;
    if (deltaUs > 0)
      acceleration = (rpm - reference.rpm) * 1000000.0f / static_cast<float>(deltaUs);
  }

  derivativePoints[derivativeHead] = { elapsedUs, rpm };
  derivativeHead = (derivativeHead + 1) % PROFILE_ACCEL_WINDOW_SAMPLES;
  if (derivativeCount < PROFILE_ACCEL_WINDOW_SAMPLES) derivativeCount++;
  return acceleration;
}

static void bufferSampleLocked(const MotorProfileSample &sample) {
  if (bufferedSamples == PROFILE_BUFFER_SAMPLES) {
    sampleTail = (sampleTail + 1) % PROFILE_BUFFER_SAMPLES;
    bufferedSamples--;
    profileSummary.droppedSamples++;
  }

  sampleBuffer[sampleHead] = sample;
  sampleHead = (sampleHead + 1) % PROFILE_BUFFER_SAMPLES;
  bufferedSamples++;
}

static void addSampleLocked(uint32_t elapsedUs, float rpm, bool pulseValid) {
  float acceleration = 0.0f;
  if (pulseValid) {
    acceleration = accelerationForLocked(elapsedUs, rpm);
  } else {
    derivativeHead = 0;
    derivativeCount = 0;
  }
  Telemetry telemetry = mbTelemetry();
  MotorProfileSample sample{ elapsedUs, rpm, acceleration, pulseValid,
                             telemetry.voltage, telemetry.current };
  bufferSampleLocked(sample);

  uint32_t elapsedMs = elapsedUs / 1000;
  profileSummary.elapsedMs = elapsedMs;
  profileSummary.totalSamples++;
  profileSummary.currentRpm = rpm;
  if (rpm > profileSummary.peakRpm) profileSummary.peakRpm = rpm;
  if (acceleration > profileSummary.peakAccelerationRpmPerSec)
    profileSummary.peakAccelerationRpmPerSec = acceleration;
  if (acceleration < profileSummary.peakDecelerationRpmPerSec)
    profileSummary.peakDecelerationRpmPerSec = acceleration;

  if (profileSummary.targetRpm == 0) return;
  float tenPercent = profileSummary.targetRpm * 0.1f;
  float ninetyPercent = profileSummary.targetRpm * 0.9f;
  if (!profileSummary.reached10Percent && rpm >= tenPercent) {
    profileSummary.reached10Percent = true;
    profileSummary.timeTo10PercentMs = elapsedMs;
  }
  if (profileSummary.reached10Percent && !profileSummary.reached90Percent && rpm >= ninetyPercent) {
    profileSummary.reached90Percent = true;
    profileSummary.timeTo90PercentMs = elapsedMs;
    profileSummary.riseTime10To90Ms = elapsedMs - profileSummary.timeTo10PercentMs;
    if (profileSummary.riseTime10To90Ms > 0) {
      profileSummary.averageAccelerationRpmPerSec =
        (0.8f * profileSummary.targetRpm * 1000.0f) /
        static_cast<float>(profileSummary.riseTime10To90Ms);
    }
  }
}

static void motorProfileTask(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(PROFILE_SAMPLE_PERIOD_MS));
    PulseSpeedSnapshot speed = getProfilePulseSpeedSnapshot();

    lockProfile();
    if (profileSummary.active) {
      uint32_t elapsedUs = micros() - profileStartUs;
      if (speed.valid && speed.pulseCount != lastObservedPulseCount) {
        lastObservedPulseCount = speed.pulseCount;
        lastSpeedValid = true;
        addSampleLocked(speed.lastEdgeUs - profileStartUs, speed.rpm, true);
      } else if (!speed.valid && lastSpeedValid) {
        lastSpeedValid = false;
        addSampleLocked(elapsedUs, 0.0f, false);
      } else {
        profileSummary.elapsedMs = elapsedUs / 1000;
      }
    }
    unlockProfile();
  }
}

void motorProfileStart() {
  PulseSpeedSnapshot speed = getProfilePulseSpeedSnapshot();

  lockProfile();
  profileSummary.active = true;
  profileSummary.sessionId++;
  clearCaptureLocked();
  profileSummary.targetRpm = config.controlMode == ControlMode::ClosedLoop ? config.targetRpm : 0;
  profileSummary.commandedAccelerationMs = config.accelerationMs;
  profileSummary.startRpm = speed.valid ? speed.rpm : 0.0f;
  profileSummary.currentRpm = profileSummary.startRpm;
  profileSummary.peakRpm = profileSummary.startRpm;
  profileStartUs = micros();
  lastObservedPulseCount = speed.pulseCount;
  lastSpeedValid = speed.valid;
  addSampleLocked(0, profileSummary.startRpm, speed.valid);
  unlockProfile();
}

void motorProfileStop() {
  lockProfile();
  if (profileSummary.active) profileSummary.elapsedMs = (micros() - profileStartUs) / 1000;
  profileSummary.active = false;
  unlockProfile();
}

void motorProfileReset() {
  lockProfile();
  profileSummary.active = false;
  profileSummary.sessionId++;
  clearCaptureLocked();
  unlockProfile();
}

size_t motorProfileRead(MotorProfileSummary &summary, MotorProfileSample *samples,
                        size_t sampleCapacity) {
  lockProfile();
  if (profileSummary.active) profileSummary.elapsedMs = (micros() - profileStartUs) / 1000;
  summary = profileSummary;

  size_t count = 0;
  while (count < sampleCapacity && bufferedSamples > 0) {
    samples[count++] = sampleBuffer[sampleTail];
    sampleTail = (sampleTail + 1) % PROFILE_BUFFER_SAMPLES;
    bufferedSamples--;
  }
  unlockProfile();
  return count;
}

void motorProfileBegin() {
  profileMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(motorProfileTask, "motor-profile", 4096, nullptr,
                          PRIO_PROFILE, nullptr, 1);
}
