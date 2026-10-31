#pragma once

#include <Arduino.h>

void otaBegin();
bool otaSetWifi(const String &ssid, const String &password);
void otaClearWifi();
bool otaRequestCheck(bool install);
bool otaInProgress();
String otaStatusJSON();
