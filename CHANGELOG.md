# Changelog

## v0.302 - 2026-11-17

- Fixed latches during stop and e-stop.
- Fixed LTO flag not being set.

## v0.301 - 2026-11-14

- Clear the previous safety-stop reason after a successful motor start, ramp start, or `clearfault`.
- Record distinct stop reasons for ramp failures, app emergency stops, and physical safety inputs.
- Preserve leading and trailing spaces in Wi-Fi credentials.
- Accept release subjects such as `SpinCoater v0.301: description` as well as exact version subjects; add a manual release trigger with version validation.
- Enable link-time optimization to reduce firmware flash usage while retaining the current OTA partition layout.

## v0.300 - 2026-10-26

Safety and robustness release. **App and bench script must send `auth=<pin>` now.**

- Added a PIN (`auth=`, `setpin=`; default `1234`, shown as `pin_default` in `status`). Only `status`, `ping`, `stop`, `estop` work before auth; five wrong tries lock out for 30 s; the session ends when the phone disconnects.
- Added a safety supervisor task: constant-speed runs stop after `max_hold_s` (default 600 s, 0 = off), stop 3 s after the phone disconnects (`link_stop`, default on), and a driver that stops answering for 1.5 s while running latches the e-stop.
- Emergency stop now retries, and while latched keeps re-sending brake/stop until the rotor is below 40 rpm; `status` reports `estop_stuck`.
- Ramps: a zero-duration segment can no longer jump up in speed (only down, e.g. to 0); start values above 0 are rejected from standstill (`ramp_start`); total length is capped at 1 h (`ramp_length`).
- Modbus calls no longer use task notifications for their replies (a stale or interrupt-triggered wake-up could be read as success).
- Task watchdog on the modbus, telemetry, ramp, comms, LED and safety tasks (5 s); the driver is told to stop on every boot; Modbus init failure restarts after 15 s instead of hanging.
- OTA: periodic checks (24 h, only when no phone is connected) only report; `ota_check` checks, `ota_install` installs. The image is marked valid after 15 s of healthy running (rollback-ready).
- `status` adds `authenticated`, `pin_default`, `max_hold_s`, `link_stop`, `driver_ok`, `estop_stuck`, `hold_remaining_s`, `last_trip`, `uptime_s`, `reset_reason`.
- New commands: `auth`, `setpin`, `max_hold_s`, `link_stop`, `ping`, `reboot`, `ota_install`. Config schema bumped to 6 (saved settings reset once).

## v0.214 - 2026-10-20

- Capped the driver current limit at 15 A (the JKBLD300V2 hardware rating; was 35).
- Profile samples now include bus voltage and current; buffer doubled to 512 samples (~10 s).
- Telemetry refresh sped up from 200 ms to 100 ms.
- Ramps whose slope exceeds `RAMP_MAX_ACCEL_RPM_S` / `RAMP_MAX_ACCEL_RAW_S` are rejected with field `ramp_accel`.
- Added `tools/spincoater_bench.py`, a guided hardware benchmark over Bluetooth.

## v0.213 - 2026-09-19

- Exposed driver pole-pair count, current limit, and PWM max as configurable settings.
- Fixed RPM calculation to use configured pole-pair count.
- Fixed `open_loop_raw` minimum bound (0 -> 100).
- Fixed ramp validation bug.
- Added reading of the driver's overcurrent alarm threshold.
- Bumped config schema version.

## v0.212 - 2026-09-18

- Added GitHub Releases OTA firmware updates.
- Added `ota_check`, `ota_status`, and `wifi_clear` commands.
- Added workflow to publish firmware.

## v0.211 - 2026-09-18

- Fixed telemetry Modbus read errors.
- Added buzzer self-test on boot.
- Added zero-RPM ramp segments.
- Made ramp setpoint failures stop and latch emergency.
- Made driver faults latch emergency until the fault is cleared.
- Added failure responses for unsuccessful stop and clear-fault commands.
- Implemented validation for stored configuration.

## v0.210 - 2026-09-13

- Added physical motor profiling through `profile=start`, `profile=stop`, `profile=reset`, and `profile` over BT.
- Captures pulse-derived RPM, acceleration, peak RPM, peak acceleration/deceleration, 10–90% rise time, average acceleration, pulse jitter, and timestamped samples.
- Added a 20 ms profiler task, 256-sample buffer, and batched profile output.
- Added buzzer patterns for emergency stop and driver fault.
- Changed to single Modbus block read for telemetry.
- Centralized motor-status conversion, safety-bypass handling, and emergency state.
- Simplified ramps to atomic `ramp=<profile>` command and removed legacy ramp commands.
- Removed the fixed BT PIN `123456`, the RS485 timing profiler, duplicate state, and unused APIs.
- Added ESP32 v2/v3 RGB LED compatibility and increased the BT command buffer to 1024 bytes.

## v0.204 - 2026-09-13

- Made SPEED GPIO14 and limit switch GPIO27.
- Implemented pulse-period RPM, frequency, jitter, glitch rejection, and stale-signal detection.
- Added pulse diagnostics to `status`.
- Removed unused telemetry and interfaces.

## v0.203 - 2026-09-13

- Attached the SPEED rising-edge interrupt and made the emergency task honor safety bypass.

## v0.202 - 2026-08-10

- Used the JKBLD300V2 `0x4000`-based Modbus register addresses.
- Fixed Modbus register offsets.

## v0.201 - 2026-08-08

- Added direct emergency and limit-switch checks before starting the motor or a ramp.
- Required both safety inputs to remain inactive for 200 ms before `clearfault` succeeds.
- Added compile-time safety bypass.

## v0.200 - 2026-08-08

- Moved the controller to FreeRTOS tasks for emergency handling, BT, Modbus, telemetry, ramps, and status LEDs.
- Added exclusive queued Modbus ownership with a higher-priority emergency queue.
- Added cached telemetry with validity/age data.
- Added the 32-segment, 20 ms piecewise-linear ramp engine.
- Added RS485 timing `profile` and `profilereset` commands.
- Added shared-state locking, task priorities, Modbus debug control, tighter bus timing, and a shorter RGB startup test.

## v0.103 - 2026-08-07

- Changed Modbus frames from 16-bit arrays to byte arrays.
- Added a true emergency stop that engages the driver brake before stopping.

## v0.102 - 2026-08-03

- Changed pins to emergency GPIO15, SPEED GPIO2, limit switch GPIO14, and buzzer GPIO4.
- Added the limit-switch safety interrupt and initialized the buzzer.
- Removed the timer-based speed counter/RPM implementation and configurable pulse count.
- Improved invalid-command error details.

## v0.101 - 2026-07-29

- Made Modbus startup failure latch emergency and halt startup.
- Removed the separate ALM input, alarm latch, alarm status, and alarm-clear path.
- Moved app development to Indriya-App.
- Moved firmware from `firmware/` to root and normalized file structure.

## v0.100 - 2026-07-26

- Replaced PWM settings with Modbus address, control mode, direction, target RPM, open-loop raw command, acceleration time, and pulses per revolution.
- Introduced `rpm`, `open_loop_raw`, `accel_ms`, `speed_pulses_per_revolution`, `dir`, and `mode` commands.
- Removed `freq`, `duty`, movement, PWM-pin, pole-pair, and pass commands/storage.
- Persisted direction, used the configured Modbus address, and widened open-loop values to 0–1000.
- Removed tracked `.pio` output and organized the firmware under `firmware/`.
- Released Android app v1.1 aligned with the firmware protocol, adding open/closed-loop controls and native motor fields and removing the PWM/pass UI.

## v0.99 - 2026-07-24

- Changed open-loop scaling to 100–1000 and acceleration limits to 300–15000 ms.
- Added serial Modbus TX/RX logging and adjusted RS485 transmit timing.
- Released Android app v1.0 with BT control, recipes, logs, settings, Compose UI, and Room storage.

## v0.98 - 2026-07-21

- Replaced direct ESP32 PWM motor control with JKBLD300V2 Modbus RTU over RS485.
- Added CRC frame handling, control/run/direction/brake/fault commands, and driver telemetry.
- Added RS485 RX GPIO16, TX GPIO17, and DE/RE GPIO5.

## v0.97 - 2026-06-16

- Changed the RGB status LED to 5 kHz, 8-bit LEDC PWM.
- Added a RGB startup self-test and revised idle colors.

## v0.96 - 2026-06-16

- Added structured JSON errors, strict numeric parsing, command overflow errors, and descriptive field aliases.
- Added configuration-size/corruption validation and safer PWM-pin rollback.
- Consolidated runtime hardware state and moved debounce outside the ISR.
- Added BT and PWM startup failure handling.

## v0.95 - 2026-05-21

- Added the hardware module, centralized pin map, emergency/alarm latches, direction/run state, RGB status, and pulse-derived RPM.
- Added emergency, alarm, and speed interrupts plus direct EN/BRK/DIR handling.
- Replaced `showparm` with `status` and added `estop`, `clearfault`, `dir`, and `polepairs`.
- Added safety-locked starts, a fixed BT PIN, and command buffer.

## v0.94 - 2026-05-14

- Split the monolithic sketch into command, configuration, I/O, motor, and globals modules.
- Added PlatformIO and ArduinoJson and converted output to structured JSON.
- Included `.pio` build and dependency files in the repository.

## v0.93 - 2026-05-14

- Added working MCPWM `start` and `stop` commands.
- Moved emergency debounce out of ISR and fixed default configuration loading.
- Avoided unnecessary preference writes and reapplied duty after frequency changes.
- Added Serial/BT `Ready` messages and stricter pass validation.

## v0.92 - 2026-05-09

- Added BT pass telemetry and reusable pass JSON generation.
- Renamed stored pass delay from `delayMs` to `delay`.

## v0.91 - 2026-05-06

- Added Preferences persistence and a versioned motor configuration with up to five passes.
- Added `addpass`, timer-based telemetry groundwork, standardized responses, and duty-zero stop behavior.
- Moved default PWM from GPIO12 to GPIO13 and excluded the emergency pin from PWM choices.

## v0.90 - 2026-05-06

- Added the ESP32 prototype with MCPWM, BT Classic, basic commands, JSON status, an emergency-button ISR, placeholder timer, motor stop, and telemetry.
