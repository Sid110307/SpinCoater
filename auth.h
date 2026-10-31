#pragma once

#include <Arduino.h>

// Per-connection PIN. Only status/ping/stop/estop work before auth=<pin>.
void authBegin();
bool authSessionOk();
void authReset();                         // call when the client disconnects
int authTry(const String &pin);           // 0 ok, 1 wrong, 2 locked out
uint32_t authLockoutRemainingS();
bool authSetPin(const String &pin);       // 4..8 digits
bool authPinIsDefault();
