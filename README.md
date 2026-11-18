# SpinCoater

ESP32 firmware for the 182 mm spin coater. Drives a JKBLD300V2 over RS485 Modbus RTU and talks to the Indriya app over Bluetooth.

## Build

```
pio run -e esp32dev
pio run -e esp32dev -t upload
```

## Release

1. Bump `FIRMWARE_VERSION` in `platformio.ini`
2. Add the version to `CHANGELOG.md`
3. Commit with the subject `v<version>` and push to `master`

The workflow builds the firmware and publishes `firmware.bin` and `manifest.json` to GitHub Releases. The app downloads the release and sends it to the controller over Bluetooth. The motor must be stopped.

## Commands

One command per line over Bluetooth. Replies are `OK` or JSON.

```
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

ota_begin=<size>,<sha256>
  Start a firmware update sent over Bluetooth (PIN required, motor stopped).
  Example: ota_begin=1048576,9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08

ota_data=<offset>,<base64>
  Write the next block of the image (up to 2048 bytes, base64 encoded). <offset> must
  equal the bytes written so far; a mismatch is ignored and the reply gives the real offset.
  Replies with {"type":"ota","state":"receiving","written":<n>,"size":<n>}.

ota_end
  Verify the SHA-256, switch to the new image and restart. If the new firmware does not
  stay up for 15 s the bootloader returns to the previous one.

ota_abort
  Cancel the update. It is also cancelled when the phone disconnects or no data
  arrives for 20 s.

ota_status
  Show OTA state and the running firmware version.
```

## LED and buzzer

| | |
|---|---|
| Red, 3 beeps | E-stop |
| Long beep | Driver fault |
| Green | Running |
| Cyan | Phone connected |
| Amber | Idle |
