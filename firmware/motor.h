#pragma once

#include "config.h"

bool startMotor();
void stopMotor();

bool changeFreq(int freq);
bool changeDuty(float duty);
bool changeLen(float len);
bool changeDur(float dur);
bool changePin(int pin);
bool addPass(float rpm, float accel, float delay);
