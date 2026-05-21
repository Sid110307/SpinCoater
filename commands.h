#pragma once

#include <Arduino.h>

#include "hardware.h"

/**
start
  Start motor.

stop
  Stop motor.

status
  Print current parameters as JSON over Serial and BT.

estop
  Emergency stop immediately.

clearfault
  Clear emergency/alarm fault state.

freq=<int>
  Set PWM frequency.
  Range: 1..MAX_FREQ
  Example: freq=1000

duty=<float>
  Set PWM duty cycle %.
  Range: 0..100
  Example: duty=45.5

move=<float>
  Move in mm.
  Range: >= 0
  Example: move=25

pwmdur=<float>
  Set PWM duration.
  Range: >= 0
  Example: pwmdur=500

pwmpin=<int>
  Set PWM output pin.
  Must be a valid and available PWM pin.
  Example: pwmpin=22

dir=<1|0>
  Set motor direction output (1: CW, 0: CCW).
  Example: dir=1

polepairs=<int>
  Set speed sensor pole-pair count used for RPM calculation.
  Example: polepairs=4

addpass=<float>,<float>,<float>
  Add a pass (rpm, acceleration, delay (ms)).
  Example: addpass=1200,50,200
*/
String parse(String input);
