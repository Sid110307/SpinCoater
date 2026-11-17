#pragma once

#include <Arduino.h>

/**
auth=<pin>
  Unlock the connection (4-8 digits, default 1234 until `setpin`). Needed before any
  command except status, ping, stop, estop. Five wrong tries lock out for 30 s.

setpin=<4..8 digits>
  Change the PIN.

ping
  Reply with {"pong":<uptime ms>}.

max_hold_s=<0..3600>
  Stop a constant-speed run (start, or after a ramp that ends above 0) after this many
  seconds. 0 turns the limit off. Default 600.

link_stop=<0|1>
  1 = stop a constant-speed run if the phone disconnects for 3 s (default). A ramp in
  progress always runs to its end.

reboot
  Restart the controller (motor must be stopped).

start
  Start motor.

stop
  Stop motor.

status
  Show current parameters as JSON.

estop
  Emergency stop immediately.

clearfault
  Clear emergency fault state and the previous safety-stop reason after driver confirmation.

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
  Range: 0, or 3..15
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

driver_packet=<hex>
  Send a single Modbus RTU diagnostic packet through the bus task (PIN required).
  Supply 6 payload bytes for automatic CRC, or 8 bytes including a valid CRC.
  Supports function 03 (1..125 registers) and 06 (approved configuration writes).
  Example: driver_packet=01 03 40 41 00 01
  Replies with type=driver_packet, result, TX/RX hex, CRC validity and elapsed_us.
  Motor, ramp, braking and OTA must be idle. Reads also work with a latched stop.
  Writes additionally require healthy stationary telemetry and clear safety inputs.
  Manual start, brake release, external-IO selection, address/baud changes and
  unsupported/out-of-range writes are rejected. Use normal controls to run.

ota_check
  Check GitHub Releases for a newer firmware (does not install).

ota_install
  Check and, if newer, download, verify (SHA-256) and install the update. The motor must be stopped.

ota_status
  Show OTA status.

wifi=<ssid>|<password>
  Save Wi-Fi credentials used for OTA checks. Spaces are preserved in SSID and password.

wifi_clear
  Remove saved OTA Wi-Fi credentials.
*/
String parse(String input);
