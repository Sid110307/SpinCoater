#pragma once

#include "config.h"

struct RampState {
  bool active;
};

void rampBegin();
bool rampStartProfile(const char *spec, const char **errField);
void rampStop();
RampState rampGetState();
