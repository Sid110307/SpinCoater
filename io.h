#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"

void getPasses(Stream& stream);
void getJSON(Stream& stream);
