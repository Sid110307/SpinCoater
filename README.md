# 182 mm spin coater firmware

This repository contains ESP32 firmware for a 182 mm spin coater. It implements configurable motor profiles, ramp control, Modbus communication, persistent configuration, and over-the-air update support.

## Features

- Recipe and motor-profile execution
- Controlled acceleration and ramp handling
- Modbus communications
- OTA firmware updates
- Serial diagnostics and configuration
- PlatformIO-based reproducible builds

## Build

### Requirements

- PlatformIO
- ESP32 development target compatible with `esp32dev`

```sh
pio run
```

Upload firmware and open the serial monitor with:

```sh
pio run --target upload
pio device monitor
```

The configured serial monitor speed is 115200 baud. Review `platformio.ini` before building; it defines the platform package, partition table, upload speed, dependencies, and firmware version.

## Releases

Push a commit to `master` with a subject such as `v0.301` or
`SpinCoater v0.301: release description`. The release workflow builds that version
and publishes `firmware.bin` and its SHA-256 manifest for Wi-Fi updates.
Alternatively, run **Firmware release** from GitHub Actions with a version tag
such as `v0.301`. The workflow must be on GitHub before it can be dispatched.
Use a new version tag for each release; publishing an existing tag fails.

Validate release version selection locally with:

```sh
python3 -m unittest discover -s tools/tests
```

## Structure

- `SpinCoater.ino` — firmware entry point
- `motor*.cpp`, `ramp.cpp` — motor and profile control
- `modbus*.cpp` — Modbus protocol and task handling
- `ota.cpp` — update support
- `config.cpp`, `hardware.cpp`, `io.cpp` — configuration and hardware abstraction
- `CHANGELOG.md` — release history

## Safety

A spin coater is rotating machinery. Validate motor direction, speed limits, lid/interlock behavior, emergency shutdown, and mechanical balance on the intended hardware before operation.

## Hardware benchmark

`tools/spincoater_bench.py` is a guided benchmark that connects over Bluetooth (`SpinCoat`), walks a safety checklist, then records open/closed-loop sweeps, step response, deceleration/regeneration, e-stop, repeatability and thermal data to `tools/bench_runs/`. Standard library only; `--simulate --yes` runs it against a built-in motor model.

```sh
python3 tools/spincoater_bench.py --label glass182
```
