#pragma once

#include <Arduino.h>

#include "config.h"

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

struct RampState {
  bool active;
  uint8_t segmentIndex;
  uint8_t segmentCount;
  uint16_t currentSetpoint;
  uint32_t elapsedMs;
  uint32_t totalMs;
};

void rampBegin();
bool rampParse(const char *spec, RampProfile &out, const char **errField);
bool rampLoad(const RampProfile &profile);

void rampBuilderReset(uint16_t startValue = 0);
void rampBuilderSetStart(uint16_t startValue);
bool rampBuilderAdd(uint16_t target, uint32_t durationMs);

bool rampStart();
bool rampParseLoadStart(const char *spec, const char **errField);
void rampStop();

RampState rampGetState();
