#pragma once

#include <Arduino.h>

void authBegin();
bool authSessionOk();
void authReset();
int authTry(const String &pin);
uint32_t authLockoutRemainingS();
bool authSetPin(const String &pin);
bool authPinIsDefault();
