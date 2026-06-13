#pragma once

#include <driver/mcpwm.h>
#include "config.h"

bool setupPWM();
bool startMotor();
void stopMotor();

bool changeFreq(int freq);
bool changeDuty(float duty);
bool changeLen(float len);
bool changeDur(float dur);
bool changePin(int pin);
bool addPass(float rpm, float accel, float delay);
