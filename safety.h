#pragma once

#include <Arduino.h>

void safetyBegin();

uint32_t safetyHoldRemainingS();
bool safetyEstopStuck();
bool safetyCommsOk();
const char *safetyLastTrip();
void safetyClearTrip();
void safetyRecordTrip(const char *reason);
