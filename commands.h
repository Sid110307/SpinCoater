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

dir=<1|0>
  Set motor direction output (1: CW, 0: CCW).
  Example: dir=1

mode=<closed_loop|open_loop>
  Set motor control mode.
  Example: mode=closed_loop

ramp=<[start|]target,durationMs;target,durationMs;...>
  Start a ramp curve (lerp from the previous point to <target> over <durationMs>.
  "<start>|" sets the initial value (default 0).
  Example: ramp=200,1000;1500,3000;1500,5000;0,2000
           (0 to 200 in 1s, to 1500 in 3s, hold 5s, to 0 in 2s)

rampload=<[start|]target,durationMs;target,durationMs;...>
  Upload a curve without starting it.

rampstart
  Start the loaded curve.

rampstop
  Stop the ramp engine (stay at last setpoint).

profile
  Print RS485 transaction timing stats as JSON (count/min/max/avg us,
  timeouts, crc_errors).

profilereset
  Reset the profiling counters.
*/
String parse(String input);
