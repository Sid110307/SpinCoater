#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"
#include "hardware.h"

void getPasses(Stream& stream);
String getJSONString();
void getJSON(Stream& stream);
