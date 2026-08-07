#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"
#include "hardware.h"

String getJSONString();
void getJSON(Stream &stream);
