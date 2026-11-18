# Changelog

## v0.304 - 2026-11-18

- Firmware updates go over Bluetooth from the app (`ota_begin`, `ota_data`, `ota_end`, `ota_abort`)
- Wi-Fi OTA removed (`wifi`, `wifi_clear`, `ota_check`, `ota_install`)
- Updates roll back if the new firmware doesn't stay up for 15 s
- Bluetooth receive buffer raised to 8 KB and command lines to 4 KB
- Profile capture ignores pulse dropouts when computing acceleration
- Release workflow checks the version, image size and tag before publishing

## v0.303 - 2026-11-17

- `driver_packet` command for sending raw Modbus RTU packets
- Packets only go out while the motor is idle; writes are limited to safe registers

## v0.302 - 2026-11-17

- Fixed stop and e-stop latching
- Fixed LTO not being applied

## v0.301 - 2026-11-14

- Stop reason clears after a successful start or `clearfault`
- Separate stop reasons for ramp failure, app e-stop and safety inputs
- Wi-Fi credentials keep leading and trailing spaces
- LTO enabled

## v0.300 - 2026-10-26

App must send `auth=<pin>` from this version on.

- PIN lock (`auth`, `setpin`, default `1234`)
- Hold limit (`max_hold_s`), stop on phone disconnect (`link_stop`), e-stop when the driver goes silent
- E-stop keeps braking until the rotor is below 40 rpm
- Ramps can't jump up from standstill; max length 1 h
- Task watchdog; driver is stopped on every boot
- OTA checks no longer install on their own; `ota_install` added
- New `status` fields and commands: `ping`, `reboot`

## v0.214 - 2026-10-20

- Current limit capped at 15 A
- Profile samples include bus voltage and current; buffer 512 samples
- Telemetry every 100 ms
- Ramps steeper than the accel limit are rejected

## v0.213 - 2026-09-19

- Pole pairs, current limit and PWM max are configurable
- Fixed RPM with non-default pole pairs, `open_loop_raw` minimum and ramp validation
- Reads the driver overcurrent threshold

## v0.212 - 2026-09-18

- OTA from GitHub Releases (`ota_check`, `ota_status`, `wifi_clear`)

## v0.211 - 2026-09-18

- Fixed telemetry read errors
- Ramp setpoint failures and driver faults latch the e-stop
- Zero-RPM ramp segments
- Buzzer self-test

## v0.210 - 2026-09-13

- Motor profiling (`profile`, `profile=start|stop|reset`)
- Buzzer patterns for e-stop and driver fault
- Single-read telemetry
- `ramp=<profile>` replaces the old ramp commands

## v0.204 - 2026-09-13

- SPEED on GPIO14, limit switch on GPIO27
- Pulse-period RPM with jitter and stale-signal detection

## v0.203 - 2026-09-13

- SPEED interrupt attached

## v0.202 - 2026-08-10

- Fixed JKBLD300V2 register addresses

## v0.201 - 2026-08-08

- Safety inputs checked before every start
- `clearfault` needs both inputs clear for 200 ms

## v0.200 - 2026-08-08

- FreeRTOS tasks
- Queued Modbus with a priority e-stop queue
- 32-segment ramp engine

## v0.103 - 2026-08-07

- E-stop engages the driver brake

## v0.102 - 2026-08-03

- New pin map and limit-switch interrupt

## v0.101 - 2026-07-29

- Modbus startup failure latches the e-stop
- App moved to Indriya-App

## v0.100 - 2026-07-26

- Modbus control settings replace PWM settings

## v0.99 - 2026-07-24

- Open-loop range 100-1000, accel 300-15000 ms

## v0.98 - 2026-07-21

- JKBLD300V2 over RS485 Modbus RTU

## v0.97 - 2026-06-16

- RGB LED on LEDC PWM

## v0.96 - 2026-06-16

- JSON errors and strict parsing

## v0.95 - 2026-05-21

- Hardware module, interrupts, `status`, `estop`, `clearfault`

## v0.94 - 2026-05-14

- PlatformIO, modules, JSON output

## v0.93 - 2026-05-14

- MCPWM start/stop

## v0.92 - 2026-05-09

- Pass telemetry

## v0.91 - 2026-05-06

- Saved configuration with up to five passes

## v0.90 - 2026-05-06

- First ESP32 prototype
