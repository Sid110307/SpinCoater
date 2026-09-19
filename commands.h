#pragma once

#include <Arduino.h>

/**
start
  Start motor.

stop
  Stop motor.

status
  Show current parameters as JSON.

estop
  Emergency stop immediately.

clearfault
  Clear emergency fault state.

rpm=<int>
  Set closed-loop target RPM.
  Range: 200..4000
  Example: rpm=1200

open_loop_raw=<int>
  Set the open-loop command raw value (10%..100% in tenths of a percent).
  Range: 100..1000
  Example: open_loop_raw=405

accel_ms=<int>
  Set acceleration time in milliseconds.
  Range: 300..15000
  Example: accel_ms=500

pole_pairs=<int>
  Set the motor pole-pair count.
  Range: 1..10
  Example: pole_pairs=4

current_limit=<int>
  Set the driver's current limit (A), 0 to restore default.
  Range: 0, or 3..35
  Example: current_limit=15

pwm_max=<int>
  Cap the maximum PWM duty %.
  Range: 10..100
  Example: pwm_max=100

dir=<1|0>
  Set motor direction output (1: CW, 0: CCW).
  Example: dir=1

mode=<closed_loop|open_loop>
  Set motor control mode.
  Example: mode=closed_loop

ramp=<[start|]target,durationMs;target,durationMs;...>
  Start a ramp curve (lerp from the previous point to <target> over <durationMs>).
  "<start>|" sets the initial value (default 0).
  Example: ramp=200,1000;1500,3000;1500,5000;0,2000
           (0 to 200 in 1s, to 1500 in 3s, hold 5s, to 0 in 2s)

profile
  Print the pulse-derived motor acceleration capture and pending samples as JSON.

profile=start
  Start a new timestamped motor acceleration capture.

profile=stop
  Stop the active capture and retain its result.

profile=reset
  Clear the capture without starting a new one.

ota_check
  Check GitHub Releases for a newer firmware and install it.

ota_status
  Show OTA status.

wifi=<ssid>|<password>
  Save Wi-Fi credentials used for OTA checks.

wifi_clear
  Remove saved OTA Wi-Fi credentials.
*/
String parse(String input);
