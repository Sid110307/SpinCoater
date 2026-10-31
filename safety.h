#pragma once

#include <Arduino.h>

// Supervisor task: constant-speed hold limit, phone-link loss stop, driver-silence latch,
// verified emergency stop, task watchdog and OTA mark-valid.
void safetyBegin();

uint32_t safetyHoldRemainingS();  // 0 when no hold timer is running or the limit is off
bool safetyEstopStuck();          // emergency latched but the rotor is still turning
bool safetyCommsOk();             // driver answered recently
const char *safetyLastTrip();     // "", "hold_timeout", "link_lost", "driver_silent"
void safetyClearTrip();
