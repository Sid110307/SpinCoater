#!/usr/bin/env bash
# One-command launcher for the spin coater benchmark.
#   ./run_bench.sh <label>        real hardware, e.g. ./run_bench.sh glass182
#   ./run_bench.sh sim            offline simulator (no hardware needed)
#   ./run_bench.sh <label> --tests open,closed,step   (extra args pass through)
set -euo pipefail
cd "$(dirname "$0")"
LABEL="${1:-run}"; shift || true

if [[ "$LABEL" == "sim" ]]; then
  exec python3 spincoater_bench.py --simulate --yes --label sim "$@"
fi

command -v bluetoothctl >/dev/null || { echo "bluetoothctl not found (install bluez)"; exit 1; }
timeout 10 bluetoothctl power on >/dev/null 2>&1 || true

find_mac() { timeout 10 bluetoothctl devices | awk '/SpinCoat/ {print $2; exit}'; }
MAC="${SPINCOAT_MAC:-$(find_mac)}"
if [[ -z "$MAC" ]]; then
  echo "SpinCoat not known yet - scanning for 15 s (power the controller on)..."
  timeout 25 bluetoothctl --timeout 15 scan on >/dev/null 2>&1 || true
  MAC="$(find_mac)"
fi
[[ -n "$MAC" ]] || { echo "Could not find 'SpinCoat'. Is the ESP32 powered and not connected to your phone?"; exit 1; }

if ! timeout 10 bluetoothctl info "$MAC" | grep -q "Paired: yes"; then
  echo "Pairing with $MAC ..."
  timeout 40 bluetoothctl pair "$MAC" || true
  timeout 10 bluetoothctl trust "$MAC" >/dev/null 2>&1 || true
fi

echo "Using $MAC"
exec python3 spincoater_bench.py --mac "$MAC" --label "$LABEL" "$@"
