#pragma once

#include <Arduino.h>

bool otaInProgress();
String otaStart(const String &value);
String otaData(const String &value);
String otaFinish();
String otaCancel();
String otaStatusJSON();
void otaService(bool connected);
