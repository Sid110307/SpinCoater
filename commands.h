#pragma once

#include <Arduino.h>

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
  Clear emergency fault state.

rpm=<int>
  Set closed-loop target RPM.
  Range: 200..4000
  Example: rpm=1200

open_loop_raw=<int>
  Set the open-loop command raw value.
  Range: 0..1000
  Example: open_loop_raw=405

accel_ms=<int>
  Set acceleration time in milliseconds.
  Range: 300..15000
  Example: accel_ms=500

speed_pulses_per_revolution=<int>
  Set speed sensor pulses per revolution.
  Range: >= 1
  Example: speed_pulses_per_revolution=4

dir=<1|0>
  Set motor direction output (1: CW, 0: CCW).
  Example: dir=1

mode=<closed_loop|open_loop>
  Set motor control mode.
  Example: mode=closed_loop
*/
String parse(String input);
