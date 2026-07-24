#pragma once

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <Preferences.h>

struct MotorConfig;

extern BluetoothSerial BT;
extern Preferences prefs;
extern MotorConfig config;
