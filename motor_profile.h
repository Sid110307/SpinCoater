#pragma once

#include "config.h"

struct MotorProfileSample {
  uint32_t elapsedUs;
  float rpm;
  float accelerationRpmPerSec;
  bool pulseValid;
  uint16_t busVoltageV;
  uint16_t busCurrent10mA;
};

struct MotorProfileSummary {
  bool active;
  uint32_t sessionId;
  uint32_t elapsedMs;
  uint32_t totalSamples;
  uint32_t droppedSamples;
  uint16_t targetRpm;
  uint16_t commandedAccelerationMs;
  float startRpm;
  float currentRpm;
  float peakRpm;
  float peakAccelerationRpmPerSec;
  float peakDecelerationRpmPerSec;
  float averageAccelerationRpmPerSec;
  bool reached10Percent;
  bool reached90Percent;
  uint32_t timeTo10PercentMs;
  uint32_t timeTo90PercentMs;
  uint32_t riseTime10To90Ms;
};

void motorProfileBegin();
void motorProfileStart();
void motorProfileStop();
void motorProfileReset();
size_t motorProfileRead(MotorProfileSummary &summary, MotorProfileSample *samples,
                        size_t sampleCapacity);
