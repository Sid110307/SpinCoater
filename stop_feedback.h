#pragma once

#include <stdint.h>

struct StopFeedback {
  uint32_t nowMs;
  uint32_t requestedMs;
  uint32_t sampledMs;
  float driverRpm;
  bool driverValid;
  float pulseRpm;
  bool pulseValid;
};

// No SPEED pulses, an old idle reading, or a failed read do not prove a stop.
inline bool stoppedFeedback(const StopFeedback &s) {
  return s.driverValid && s.sampledMs != 0 && s.nowMs - s.sampledMs <= 500 &&
         (!s.requestedMs || static_cast<int32_t>(s.sampledMs - s.requestedMs) >= 0) &&
         s.driverRpm >= 0 && s.driverRpm < 40 &&
         (!s.pulseValid || (s.pulseRpm >= 0 && s.pulseRpm < 40));
}

class StopConfirmation {
  uint32_t firstSampleMs = 0;
  bool observing = false;
public:
  void reset() { observing = false; }
  bool update(const StopFeedback &s) {
    if (!stoppedFeedback(s)) { reset(); return false; }
    if (!observing) { observing = true; firstSampleMs = s.sampledMs; }
    // Require distinct, post-request speed observations spanning at least 100 ms.
    return s.sampledMs - firstSampleMs >= 100;
  }
};
