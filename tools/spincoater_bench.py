#!/usr/bin/env python3
"""Guided hardware benchmark for the 182 mm spin coater (firmware >= 0.300; pass --pin).

Talks to the ESP32 over Bluetooth SPP ("SpinCoat"), walks through a safety
checklist, runs a set of motor tests, and records everything to a run folder:

    bench_runs/<timestamp>_<label>/
        metadata.json   checklist answers, firmware status, settings
        samples.csv     every profiler sample (rpm, accel, bus V, bus I)
        status.csv      polled status (fault, ramp, running, ...)
        results.json    computed metrics per test
        report.md       human-readable summary
        *.png           plots (only if matplotlib is installed)

Speed comes from the driver's SPEED (Hall) pulse measured by the ESP32 at
20 ms resolution, so no external tachometer is needed. A handheld tach is only
used as an optional static cross-check.

Standard library only. Examples:

    ./spincoater_bench.py --label glass182            # auto-find "SpinCoat"
    ./spincoater_bench.py --mac AA:BB:CC:DD:EE:FF
    ./spincoater_bench.py --port /dev/rfcomm0
    ./spincoater_bench.py --simulate --yes            # no hardware, test the script
"""
import argparse
import csv
import json
import math
import os
import random
import re
import select
import socket
import statistics
import subprocess
import sys
import termios
import threading
import time
import tty
from datetime import datetime

# --------------------------------------------------------------------------
# Limits (match the firmware / JKBLD300V2 manual)
# --------------------------------------------------------------------------
RPM_MIN_CL = 200
RPM_MAX_CL = 4000
SUPPLY_V_MIN, SUPPLY_V_WARN, SUPPLY_V_MAX = 24.0, 50.5, 52.0   # manual port table
DRIVER_I_MAX = 15.0
RAMP_MAX_RPM_S = 2000     # firmware RAMP_MAX_ACCEL_RPM_S


class Abort(Exception):
    pass


# --------------------------------------------------------------------------
# Transport
# --------------------------------------------------------------------------
class Link:
    """Line-oriented request/response link. All commands are serialised."""

    def __init__(self):
        self.lock = threading.RLock()
        self.buf = b""

    # subclass API
    def _write(self, data): raise NotImplementedError
    def _read(self, timeout): raise NotImplementedError
    def close(self): pass

    def _readline(self, timeout):
        end = time.monotonic() + timeout
        while True:
            i = self.buf.find(b"\n")
            if i >= 0:
                line, self.buf = self.buf[:i], self.buf[i + 1:]
                return line.decode(errors="replace").strip()
            left = end - time.monotonic()
            if left <= 0:
                raise TimeoutError("no response from controller")
            self.buf += self._read(left)

    def _response(self, timeout):
        while True:
            line = self._readline(timeout)
            if line == "OK" or line.startswith("{"):
                return line

    def cmd(self, line, timeout=2.0):
        """Send one command, return OK / JSON text."""
        return self.cmds([line], timeout)[0]

    def cmds(self, lines, timeout=2.0):
        """Send several commands in ONE write (so they run back-to-back)."""
        with self.lock:
            self.buf = b""
            self._write(("".join(l + "\n" for l in lines)).encode())
            return [self._response(timeout) for _ in lines]

    def ok(self, line, timeout=2.0):
        r = self.cmd(line, timeout)
        if r != "OK":
            raise Abort(f"command '{line}' failed: {r}")

    def jcmd(self, line, timeout=2.0):
        r = self.cmd(line, timeout)
        try:
            d = json.loads(r)
        except ValueError:
            raise Abort(f"bad reply to '{line}': {r!r}")
        if "error" in d:
            raise Abort(f"command '{line}' failed: {r}")
        return d


class RfcommLink(Link):
    def __init__(self, mac, channel=1):
        super().__init__()
        self.sock = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM,
                                  socket.BTPROTO_RFCOMM)
        self.sock.settimeout(10)
        self.sock.connect((mac, channel))

    def _write(self, data): self.sock.sendall(data)

    def _read(self, timeout):
        r, _, _ = select.select([self.sock], [], [], timeout)
        if not r:
            return b""
        d = self.sock.recv(4096)
        if not d:
            raise Abort("Bluetooth link closed by controller")
        return d

    def close(self): self.sock.close()


class TtyLink(Link):
    def __init__(self, path):
        super().__init__()
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(self.fd)

    def _write(self, data): os.write(self.fd, data)

    def _read(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        return os.read(self.fd, 4096) if r else b""

    def close(self): os.close(self.fd)


def find_mac(name="SpinCoat"):
    try:
        out = subprocess.run(["bluetoothctl", "devices"], capture_output=True,
                             text=True, timeout=10).stdout
    except Exception:
        return None
    for line in out.splitlines():
        m = re.match(r"Device\s+([0-9A-F:]{17})\s+(.*)", line, re.I)
        if m and name.lower() in m.group(2).lower():
            return m.group(1)
    return None


# --------------------------------------------------------------------------
# Simulator (firmware + motor model) so the script can be tested offline
# --------------------------------------------------------------------------
class SimLink(Link):
    def __init__(self, J=0.0086, ke=0.119, vbus=48.0, imax=15.0, seed=1):
        super().__init__()
        self.J, self.ke, self.vbus, self.imax = J, ke, vbus, imax
        self.R = 0.25
        self.rng = random.Random(seed)
        self.t = time.monotonic()
        self.w = 0.0                # rad/s
        self.run = False
        self.mode = "open_loop"
        self.target_rpm, self.raw, self.accel_ms = 1200, 405, 300
        self.pp, self.cur_lim = 4, 0
        self.emerg = False
        self.fault = 0
        self.prof_t0 = None
        self.samples = []
        self.last_sample_t = 0.0
        self.ramp = None
        self.i_a = 0.0
        self.out = b""
        self.vmax_seen = vbus
        self.brake = False
        self._was_valid = False

    def _setpoint_rpm(self):
        if self.mode == "closed_loop":
            return self.target_rpm
        return self.raw * 3.85   # 3850 rpm at raw 1000

    def _step(self):
        now = time.monotonic()
        dt = now - self.t
        self.t = now
        n = max(1, int(dt / 0.005))
        h = dt / n
        for _ in range(n):
            if self.ramp:
                el = (now - self.ramp[0]) * 1000
                sp = self.ramp[1](el)
                if sp is None:
                    self.ramp = None
                    self.run = False
                else:
                    if self.mode == "closed_loop":
                        self.target_rpm = max(RPM_MIN_CL, sp) if sp else 0
                    else:
                        self.raw = sp
                    if sp == 0:
                        self.run = False
            drag = 0.012 + 0.0000045 * self.w * 1.0 + 1.9e-7 * self.w ** 2
            if self.run and not self.emerg:
                sp_w = self._setpoint_rpm() * 2 * math.pi / 60
                # driver ramp limit (accel_ms for rated 4000 rpm)
                rate = (4000 * 2 * math.pi / 60) / (self.accel_ms / 1000)
                want = min(sp_w, self.w + rate * h) if sp_w > self.w else max(sp_w, self.w - rate * h)
                vemf = self.ke * self.w
                kp = 8.0
                i_cmd = max(-self.imax, min(self.imax, kp * (want - self.w)))
                # voltage headroom
                head = self.vbus - vemf
                i_cmd = min(i_cmd, max(0.0, head / self.R))
                tq = self.ke * i_cmd
                self.i_a = abs(i_cmd) * max(0.02, (vemf + abs(i_cmd) * self.R) / self.vbus)
            elif self.brake or self.emerg:
                tq = -self.ke * min(self.imax, 8.0) if self.w > 1 else 0.0
                self.i_a = 8.0 if self.w > 1 else 0.0
                # regen pumps the bus
                self.vmax_seen = max(self.vmax_seen, self.vbus + 0.0004 * self.J * self.w ** 2 / 8)
            else:
                tq = -self.ke * 1.2 if self.w > 1 else 0.0   # slow stop
                self.i_a = 0.0
            self.w = max(0.0, self.w + (tq - drag) / self.J * h)
            if self.w < 0.05:
                self.w = 0.0
        # sampling every 20 ms while profiling
        if self.prof_t0 is not None:
            while now - self.last_sample_t >= 0.02:
                self.last_sample_t += 0.02
                el = int((self.last_sample_t - self.prof_t0) * 1e6)
                rpm = self.w * 60 / (2 * math.pi) * (1 + self.rng.gauss(0, 0.002))
                valid = rpm > 60
                if not valid and not self._was_valid:
                    continue
                self._was_valid = valid
                prev = self.samples[-1][1] if self.samples else 0.0
                acc = (rpm - prev) / 0.02 if self.samples else 0.0
                v = round(self.vbus - 0.05 * self.i_a + self.rng.uniform(-0.4, 0.4)) \
                    if not (self.brake and self.w > 1) else round(self.vmax_seen)
                self.samples.append([el, round(rpm, 1) if valid else 0.0, round(acc, 1),
                                     valid, int(v), int(self.i_a * 100)])

    def _status(self):
        return {"firmware_version": "sim", "control_mode": self.mode,
                "target_rpm": self.target_rpm, "open_loop_raw": self.raw,
                "accel_ms": self.accel_ms, "pole_pairs": self.pp,
                "current_limit_amps": self.cur_lim, "pwm_max_percent": 100,
                "running": self.run, "emergency": self.emerg,
                "motor_voltage": round(self.vbus), "motor_current": round(self.i_a, 2),
                "motor_power": round(self.vbus * self.i_a, 1),
                "motor_speed": int(self.w * 60 / (2 * math.pi)),
                "motor_fault": self.fault, "telemetry_valid": True,
                "overcurrent_alarm_amps": 20, "overcurrent_alarm_valid": True,
                "ramp": {"active": self.ramp is not None}}

    def _ramp_fn(self, spec):
        start = 0
        if "|" in spec:
            a, spec = spec.split("|", 1)
            start = int(a)
        segs = [tuple(int(x) for x in s.split(",")) for s in spec.split(";")]
        total = sum(d for _, d in segs)

        def f(el):
            if el >= total:
                return None
            acc, s0 = 0, start
            for tgt, d in segs:
                if d and el < acc + d:
                    return int(s0 + (tgt - s0) * (el - acc) / d)
                acc += d
                s0 = tgt
            return s0
        return f

    def _handle(self, line):
        self._step()
        low = line.lower()
        if low == "status":
            return json.dumps(self._status())
        if low == "profile":
            s, self.samples = self.samples[:48], self.samples[48:]
            return json.dumps({"type": "profile", "capture": {
                "active": self.prof_t0 is not None, "dropped_samples": 0,
                "sample_count": len(s)}, "speed_pulse": {}, "samples": s})
        if low == "profile=start":
            self.samples = []
            self.prof_t0 = self.t
            self.last_sample_t = self.t
            return "OK"
        if low in ("profile=stop", "profile=reset"):
            self.prof_t0 = None if low == "profile=stop" else None
            self.samples = [] if low == "profile=reset" else self.samples
            return "OK"
        if low == "start":
            if self.emerg:
                return '{"error":"SAFETY_LOCKED"}'
            self.run = True
            self.brake = False
            return "OK"
        if low == "stop":
            self.ramp = None
            self.run = False
            return "OK"
        if low == "estop":
            self.ramp = None
            self.emerg = True
            self.run = False
            self.brake = True
            return "OK"
        if low == "clearfault":
            self.emerg = False
            self.brake = False
            self.run = False
            return "OK"
        if "=" in low:
            k, v = line.split("=", 1)
            k = k.strip().lower()
            try:
                if k == "rpm":
                    self.target_rpm = int(v)
                elif k == "open_loop_raw":
                    self.raw = int(v)
                elif k == "accel_ms":
                    self.accel_ms = int(v)
                elif k == "mode":
                    self.mode = v.strip()
                elif k == "pole_pairs":
                    self.pp = int(v)
                elif k == "current_limit":
                    self.imax = float(v) if int(v) else 15.0
                    self.cur_lim = int(v)
                elif k == "ramp":
                    f = self._ramp_fn(v)
                    rate = RAMP_MAX_RPM_S if self.mode == "closed_loop" else 520
                    prev, s0 = 0, 0
                    if "|" in v:
                        s0 = int(v.split("|")[0])
                    for tgt, d in [tuple(int(x) for x in s.split(","))
                                   for s in v.split("|")[-1].split(";")]:
                        if d and abs(tgt - s0) * 1000 > rate * d:
                            return '{"error":"INVALID_VALUE","field":"ramp_accel"}'
                        s0 = tgt
                    self.ramp = (self.t, f)
                    self.run = True
                else:
                    return '{"error":"INVALID_CMD"}'
            except ValueError:
                return '{"error":"INVALID_VALUE"}'
            return "OK"
        return '{"error":"INVALID_CMD"}'

    def _write(self, data):
        for line in data.decode().splitlines():
            if line.strip():
                self.out += (self._handle(line.strip()) + "\n").encode()

    def _read(self, timeout):
        time.sleep(0.01)    # emulate bluetooth latency
        d, self.out = self.out, b""
        return d


# --------------------------------------------------------------------------
# Recorder: background thread draining the firmware profiler + status
# --------------------------------------------------------------------------
class Recorder:
    def __init__(self, link, outdir, limits):
        self.link = link
        self.limits = limits
        self.samples = []            # dicts
        self.status_log = []
        self.lock = threading.Lock()
        self.tag = ""
        self.stop_evt = threading.Event()
        self.thread = None
        self.alarm = None            # reason string if safety tripped
        self.motor_expected = False
        self.run_started = 0.0
        self.last_valid_rpm_t = time.monotonic()
        self.dropped = 0
        self.last_status = {}
        self.sf = open(os.path.join(outdir, "samples.csv"), "w", newline="")
        self.sw = csv.writer(self.sf)
        self.sw.writerow(["tag", "t_ms", "rpm", "accel_rpm_s", "pulse_valid",
                          "bus_v", "bus_a"])
        self.tf = open(os.path.join(outdir, "status.csv"), "w", newline="")
        self.tw = csv.writer(self.tf)
        self.tw.writerow(["tag", "host_t", "running", "emergency", "fault", "ramp",
                          "v", "a", "w", "driver_rpm"])

    def start(self):
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def shutdown(self):
        self.stop_evt.set()
        if self.thread:
            self.thread.join(timeout=3)
        self.sf.close()
        self.tf.close()

    def _ingest(self, d):
        cap = d.get("capture", {})
        self.dropped = max(self.dropped, cap.get("dropped_samples", 0))
        for s in d.get("samples", []):
            if len(s) < 6:
                continue
            rec = {"t": s[0] / 1000.0, "rpm": float(s[1]), "acc": float(s[2]),
                   "valid": bool(s[3]), "v": float(s[4]), "a": s[5] / 100.0}
            with self.lock:
                self.samples.append(rec)
            self.sw.writerow([self.tag, round(rec["t"], 2), rec["rpm"], rec["acc"],
                              int(rec["valid"]), rec["v"], rec["a"]])
            if rec["valid"] and rec["rpm"] > 50:
                self.last_valid_rpm_t = time.monotonic()
            if rec["a"] > self.limits["max_current"]:
                self.alarm = f"bus current {rec['a']:.1f} A > {self.limits['max_current']} A"

    def _run(self):
        n = 0
        while not self.stop_evt.is_set():
            try:
                d = self.link.jcmd("profile")
                self._ingest(d)
                n += 1
                if n % 3 == 0:
                    self._poll_status()
            except (TimeoutError, Abort) as e:
                self.alarm = f"link error: {e}"
                return
            self.stop_evt.wait(0.15)

    def _poll_status(self):
        s = self.link.jcmd("status")
        self.last_status = s
        self.tw.writerow([self.tag, round(time.monotonic(), 3), s.get("running"),
                          s.get("emergency"), s.get("motor_fault"),
                          s.get("ramp", {}).get("active"), s.get("motor_voltage"),
                          s.get("motor_current"), s.get("motor_power"),
                          s.get("motor_speed")])
        if s.get("motor_fault"):
            self.alarm = f"driver fault code {s['motor_fault']}"
        if s.get("emergency") and self.motor_expected:
            self.alarm = "emergency latched"
        if (self.motor_expected and s.get("running") and
                time.monotonic() - self.run_started > 3 and
                time.monotonic() - self.last_valid_rpm_t > 3):
            self.alarm = "no SPEED pulses for 3 s while running (Hall/SPEED wiring?)"

    def mark(self):
        with self.lock:
            return len(self.samples)

    def since(self, idx):
        with self.lock:
            return list(self.samples[idx:])

    def check(self):
        if self.alarm:
            raise Abort(self.alarm)


# --------------------------------------------------------------------------
# Small helpers
# --------------------------------------------------------------------------
def ask(prompt, yes=False):
    if yes:
        print(f"  [auto-yes] {prompt}")
        return True
    while True:
        a = input(f"  {prompt} [y/N] ").strip().lower()
        if a in ("y", "yes"):
            return True
        if a in ("n", "no", ""):
            return False


def ask_float(prompt, yes=False, default=None):
    if yes:
        return default
    while True:
        a = input(f"  {prompt} ").strip()
        if not a:
            return default
        try:
            return float(a)
        except ValueError:
            pass


def mean(x): return statistics.fmean(x) if x else float("nan")
def sd(x): return statistics.pstdev(x) if len(x) > 1 else 0.0


def window(samples, t0, t1):
    return [s for s in samples if t0 <= s["t"] <= t1]


def interp_time(samples, level, rising=True, after=0.0):
    """First time rpm crosses `level` (linearly interpolated)."""
    prev = None
    for s in samples:
        if s["t"] < after:
            prev = s
            continue
        if prev is None and ((rising and s["rpm"] >= level) or (not rising and s["rpm"] <= level)):
            return s["t"]
        if prev is not None:
            a, b = prev["rpm"], s["rpm"]
            if (rising and a < level <= b) or (not rising and a > level >= b):
                f = (level - a) / (b - a) if b != a else 0
                return prev["t"] + f * (s["t"] - prev["t"])
        prev = s
    return None


# --------------------------------------------------------------------------
# Bench
# --------------------------------------------------------------------------
class Bench:
    def __init__(self, args, link, outdir):
        self.a = args
        self.link = link
        self.out = outdir
        self.rec = Recorder(link, outdir, {"max_current": args.max_current})
        self.results = {}
        self.meta = {"label": args.label, "started": datetime.now().isoformat(),
                     "checklist": {}, "inputs": {}}
        self.status0 = {}
        self.supply_v = None

    # ---- safety ---------------------------------------------------------
    def stop_motor(self):
        self.rec.motor_expected = False
        for attempt in range(3):
            try:
                if self.link.cmd("stop") == "OK":
                    return True
            except Exception:
                pass
        try:
            self.link.cmd("estop")
        except Exception:
            pass
        return False

    def wait(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.rec.check()
            time.sleep(0.05)

    def wait_settled_stop(self, timeout=25):
        """Wait until the driver reports ~0 rpm (polls status)."""
        t_end = time.monotonic() + timeout
        quiet = 0
        while time.monotonic() < t_end:
            self.rec.check()
            time.sleep(0.25)
            st = self.link.jcmd("status")
            if st.get("motor_speed", 9999) < 30:
                quiet += 1
                if quiet >= 3:
                    return True
            else:
                quiet = 0
        return False

    # ---- checklist -------------------------------------------------------
    def preflight(self):
        a = self.a
        print("\n=== PRE-FLIGHT CHECKLIST ===")
        print("Every item must be YES. Anything else aborts before the motor moves.\n")
        items = [
            ("estop", "Emergency-stop button is wired, released, and within reach of your hand?"),
            ("lid", "Lid / interlock (limit switch) is closed and the guard bowl is installed?"),
            ("clear", "Area is clear: nobody within 1 m and no loose items near the chuck?"),
            ("chuck", "Chuck is bolted down, hub grub screw/nut tight, and spins freely by hand?"),
            ("substrate", "Substrate for this run is mounted and clamped/vacuumed (or bare chuck, per --label)?"),
            ("balance", "You have checked the substrate is centred (<0.5 mm offset)?"),
            ("sw1", f"Driver SW1 matches pole pairs (OFF = 4 pairs, ON = 2 pairs; firmware expects "
                    f"{self.status0.get('pole_pairs')})?"),
            ("wiring", "Motor U/V/W and Hall wires are secure; SPEED output is wired to ESP32 GPIO14?"),
            ("supply", "Power supply is rated >= 48 V and >= 16 A (or battery) and its output is ON?"),
            ("psv", "You know where the P-sv current knob is set (peak current limit)?"),
            ("cool", "Driver heatsink has airflow / is cool to touch (<40 C)?"),
        ]
        for key, text in items:
            ok = ask(text, a.yes)
            self.meta["checklist"][key] = ok
            if not ok:
                raise Abort(f"checklist item '{key}' not confirmed")

        print("\n  Values to record (Enter to skip):")
        self.meta["inputs"]["supply_v_meter"] = ask_float(
            "Supply voltage at driver terminals measured with multimeter (V):", a.yes, 48.0)
        self.meta["inputs"]["psv_amps"] = ask_float(
            "P-sv knob setting (A):", a.yes, 15.0)
        self.meta["inputs"]["chuck_plus_substrate_g"] = ask_float(
            "Chuck + substrate mass (g):", a.yes, None)
        self.meta["inputs"]["ambient_c"] = ask_float("Ambient temp (C):", a.yes, None)
        self.meta["inputs"]["heatsink_c_start"] = ask_float(
            "Heatsink temp now, IR thermometer (C):", a.yes, None)
        sv = self.meta["inputs"]["supply_v_meter"]
        self.supply_v = sv
        if sv is not None:
            if sv > SUPPLY_V_MAX:
                raise Abort(f"supply {sv} V exceeds {SUPPLY_V_MAX} V (manual port-table limit)")
            if sv > SUPPLY_V_WARN:
                print(f"  WARNING: {sv} V leaves <2 V margin to the {SUPPLY_V_MAX} V limit; "
                      "braking will likely trip overvoltage. 48 V recommended.")
            if sv < SUPPLY_V_MIN:
                raise Abort(f"supply {sv} V is below {SUPPLY_V_MIN} V")

        # automatic checks
        print("\n  Automatic checks:")
        s = self.link.jcmd("status")
        self.status0 = s
        self.meta["status_at_start"] = s
        problems = []
        if s.get("emergency"):
            problems.append("controller reports EMERGENCY latched (press/release e-stop, close lid, send clearfault)")
        if not s.get("telemetry_valid"):
            problems.append("driver telemetry not valid (RS485 wiring / driver power?)")
        if s.get("motor_fault"):
            problems.append(f"driver fault code {s['motor_fault']}")
        if s.get("running"):
            problems.append("motor is already running")
        v = s.get("motor_voltage", 0)
        print(f"   firmware {s.get('firmware_version')}, bus {v} V, "
              f"poles {s.get('pole_pairs')}, mode {s.get('control_mode')}, "
              f"driver OC alarm {s.get('overcurrent_alarm_amps')} A")
        if v and sv and abs(v - sv) > 3:
            problems.append(f"driver bus voltage {v} V differs from your meter {sv} V by >3 V")
        if v and v > SUPPLY_V_MAX:
            problems.append(f"driver bus voltage {v} V > {SUPPLY_V_MAX} V")
        if s.get("firmware_version", "0") != "sim" and \
                tuple(int(x) for x in re.findall(r"\d+", s.get("firmware_version", "0.0"))[:2]) < (0, 214):
            problems.append("firmware older than 0.214: profile samples lack bus V/I")
        if problems:
            for p in problems:
                print("   PROBLEM:", p)
            raise Abort("pre-flight automatic checks failed")
        print("   all good.")

        # settings applied once
        self.link.ok(f"current_limit={min(int(a.current_limit), 15)}")
        self.link.ok("accel_ms=300")
        if self.meta["inputs"].get("psv_amps") and self.meta["inputs"]["psv_amps"] > 15:
            raise Abort("P-sv above the 15 A hardware rating")

    # ---- run one motion --------------------------------------------------
    def capture(self, tag, start_cmds, duration_s, stop_when=None, motor_on=True):
        """Reset+start profiler together with start_cmds (one write), record for
        duration_s, return samples (t relative to profile start, in ms)."""
        self.rec.tag = tag
        self.link.ok("profile=reset")
        idx = self.rec.mark()
        replies = self.link.cmds(["profile=start"] + start_cmds)
        if replies[0] != "OK":
            raise Abort(f"profile=start failed: {replies[0]}")
        for c, r in zip(start_cmds, replies[1:]):
            if r != "OK":
                raise Abort(f"'{c}' failed: {r}")
        self.rec.motor_expected = motor_on
        self.rec.run_started = time.monotonic()
        self.rec.last_valid_rpm_t = time.monotonic()
        t_end = time.monotonic() + duration_s
        while time.monotonic() < t_end:
            self.rec.check()
            time.sleep(0.05)
            if stop_when and stop_when(self.rec.since(idx)):
                break
        return idx

    def finish_capture(self, idx):
        self.link.cmd("profile=stop")
        self.wait(0.6)          # let the recorder drain the tail
        return self.rec.since(idx)

    def set_closed(self, rpm, accel_ms):
        self.link.ok("mode=closed_loop")
        self.link.ok(f"rpm={int(rpm)}")
        self.link.ok(f"accel_ms={int(accel_ms)}")

    def set_open(self, raw, accel_ms):
        self.link.ok("mode=open_loop")
        self.link.ok(f"open_loop_raw={int(raw)}")
        self.link.ok(f"accel_ms={int(accel_ms)}")

    # ---- metrics -----------------------------------------------------------
    def step_metrics(self, samples, target, hold_s):
        if len(samples) < 10:
            return {"error": "too few samples"}
        tmax = samples[-1]["t"]
        w = window(samples, tmax - 1500, tmax)
        plateau = mean([s["rpm"] for s in w])
        if plateau < 50:
            return {"error": "motor never reached speed", "plateau_rpm": plateau}
        t10 = interp_time(samples, 0.1 * plateau)
        t90 = interp_time(samples, 0.9 * plateau)
        peak = max(s["rpm"] for s in samples)
        m = {"target_rpm": target, "plateau_rpm": round(plateau, 1),
             "target_error_pct": round((plateau - target) / target * 100, 2) if target else None,
             "plateau_std_rpm": round(sd([s["rpm"] for s in w]), 2),
             "plateau_pp_rpm": round(max(s["rpm"] for s in w) - min(s["rpm"] for s in w), 1),
             "first_motion_ms": None, "t10_ms": t10, "t90_ms": t90,
             "rise_10_90_ms": (t90 - t10) if (t10 and t90) else None,
             "overshoot_pct": round(max(0.0, (peak - plateau) / plateau * 100), 2)}
        mv = [s for s in samples if s["rpm"] > 30]
        m["first_motion_ms"] = mv[0]["t"] if mv else None
        for band, key in ((0.02, "settle_2pct_ms"), (0.01, "settle_1pct_ms")):
            ts = None
            for s in reversed(samples):
                if abs(s["rpm"] - plateau) > band * plateau:
                    ts = s["t"]
                    break
            m[key] = ts if ts is not None else 0.0
        if m["rise_10_90_ms"]:
            m["avg_accel_rpm_s"] = round(0.8 * plateau / (m["rise_10_90_ms"] / 1000), 0)
        m["peak_accel_rpm_s"] = round(max(s["acc"] for s in samples), 0)
        ia = [s["a"] for s in samples]
        m["peak_current_a"] = max(ia)
        m["plateau_current_a"] = round(mean([s["a"] for s in w]), 2)
        m["plateau_power_w"] = round(mean([s["a"] * s["v"] for s in w]), 1)
        m["bus_v_min"] = min(s["v"] for s in samples if s["v"] > 0)
        m["bus_v_max"] = max(s["v"] for s in samples)
        # inertia from energy balance across 10-90 %
        if t10 and t90 and m["plateau_current_a"] is not None:
            seg = window(samples, t10, t90)
            if len(seg) > 4:
                eta = 0.85
                w_plat = plateau * 2 * math.pi / 60
                e_in = 0.0
                for p, q in zip(seg, seg[1:]):
                    dt = (q["t"] - p["t"]) / 1000.0
                    w_mid = (p["rpm"] + q["rpm"]) / 2 * 2 * math.pi / 60
                    v = (p["v"] + q["v"]) / 2
                    i = (p["a"] + q["a"]) / 2
                    p_loss = v * m["plateau_current_a"] * (w_mid / w_plat) if w_plat else 0
                    e_in += (eta * v * i - p_loss) * dt
                w1 = 0.1 * w_plat
                w2 = 0.9 * w_plat
                if w2 > w1 and e_in > 0:
                    m["inertia_est_kgm2"] = round(2 * e_in / (w2 ** 2 - w1 ** 2), 5)
        for k in ("t10_ms", "t90_ms", "rise_10_90_ms", "first_motion_ms",
                  "settle_2pct_ms", "settle_1pct_ms"):
            if m.get(k) is not None:
                m[k] = round(m[k], 0)
        return m

    # ---- tests --------------------------------------------------------------
    def t_baseline(self):
        print("\n--- T1 idle baseline (motor off, 6 s) ---")
        idx = self.capture("idle", [], 6, motor_on=False)
        s = self.finish_capture(idx)
        st = self.link.jcmd("status")
        self.results["idle"] = {"bus_v": st.get("motor_voltage"),
                                "bus_a": st.get("motor_current"),
                                "spurious_pulse_samples": len(s)}
        print("   bus", st.get("motor_voltage"), "V,", st.get("motor_current"), "A,",
              len(s), "spurious samples")

    def hold_stats(self, samples, t_from_ms):
        w = [s for s in samples if s["t"] >= t_from_ms]
        return {"rpm": mean([s["rpm"] for s in w]), "std": sd([s["rpm"] for s in w]),
                "pp": (max(s["rpm"] for s in w) - min(s["rpm"] for s in w)) if w else 0,
                "a": mean([s["a"] for s in w]), "v": mean([s["v"] for s in w]),
                "n": len(w)}

    def t_open_sweep(self):
        print("\n--- T2 open-loop calibration sweep ---")
        have_tach = (not self.a.yes) and ask("Do you have a handheld tach for a static cross-check?")
        raws = list(range(100, 1001, 100))
        hold = self.a.hold
        self.set_open(raws[0], 1500)
        self.rec.tag = "open_sweep"
        self.link.ok("profile=reset")
        idx = self.rec.mark()
        self.link.cmds(["profile=start", "start"])
        self.rec.motor_expected = True
        self.rec.run_started = time.monotonic()
        self.rec.last_valid_rpm_t = time.monotonic()
        rows = []
        for i, raw in enumerate(raws):
            if i:
                self.link.ok(f"open_loop_raw={raw}")
            t_step = time.monotonic()
            self.wait(hold)
            samples = self.rec.since(idx)
            tmax = samples[-1]["t"] if samples else 0
            hs = self.hold_stats(samples, tmax - 2500)
            row = {"raw": raw, "pct": raw / 10, "rpm": round(hs["rpm"], 1),
                   "std": round(hs["std"], 1), "bus_a": round(hs["a"], 2),
                   "bus_v": round(hs["v"], 1)}
            if have_tach and raw in (500, 1000):
                row["tach_rpm"] = ask_float(f"  Tach reads at {raw/10:.0f}% (rpm, Enter skip):", False, None)
            rows.append(row)
            print(f"   raw {raw:4d}: {row['rpm']:7.1f} rpm  +-{row['std']:.1f}  "
                  f"{row['bus_a']:.2f} A @ {row['bus_v']:.0f} V")
        self.link.cmd("stop")
        self.rec.motor_expected = False
        self.wait_settled_stop()
        self.finish_capture(idx)
        xs = [r["raw"] for r in rows if r["rpm"] > 100]
        ys = [r["rpm"] for r in rows if r["rpm"] > 100]
        if len(xs) >= 3:
            mx, my = mean(xs), mean(ys)
            slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
            icpt = my - slope * mx
            ss_res = sum((y - (slope * x + icpt)) ** 2 for x, y in zip(xs, ys))
            ss_tot = sum((y - my) ** 2 for y in ys)
            r2 = 1 - ss_res / ss_tot if ss_tot else 1
            top = rows[-1]["rpm"]
            vtop = rows[-1]["bus_v"] or self.supply_v or 48
            ke = vtop / (top * 2 * math.pi / 60) if top else None
            self.results["open_sweep"] = {
                "rows": rows, "rpm_per_raw": round(slope, 3), "intercept_rpm": round(icpt, 1),
                "r2": round(r2, 5), "top_rpm": top, "ke_v_s_rad_est": round(ke, 4) if ke else None,
                "max_rpm_at_supply_v": vtop}
            print(f"   slope {slope:.3f} rpm/raw, intercept {icpt:.0f} rpm, R2 {r2:.4f}, "
                  f"top {top} rpm @ {vtop:.0f} V")
            for r in rows:
                if r.get("tach_rpm"):
                    err = (r["rpm"] - r["tach_rpm"]) / r["tach_rpm"] * 100
                    print(f"   tach check raw {r['raw']}: SPEED-pulse {r['rpm']} vs tach "
                          f"{r['tach_rpm']} -> {err:+.2f}%  "
                          f"{'(CHECK pole-pairs!)' if abs(err) > 3 else ''}")
                    r["tach_err_pct"] = round(err, 2)

    def t_closed_sweep(self):
        print("\n--- T3 closed-loop accuracy sweep ---")
        levels = [r for r in (200, 500, 1000, 1500, 2000, 2500, 3000, 3500, 3800, 4000)
                  if r <= self.a.max_rpm]
        self.set_closed(levels[0], 1500)
        self.rec.tag = "closed_sweep"
        self.link.ok("profile=reset")
        idx = self.rec.mark()
        self.link.cmds(["profile=start", "start"])
        self.rec.motor_expected = True
        self.rec.run_started = time.monotonic()
        self.rec.last_valid_rpm_t = time.monotonic()
        rows = []
        for i, r in enumerate(levels):
            if i:
                self.link.ok(f"rpm={r}")
            self.wait(self.a.hold + (2 if r > 2000 else 0))
            samples = self.rec.since(idx)
            tmax = samples[-1]["t"]
            hs = self.hold_stats(samples, tmax - 2500)
            err = (hs["rpm"] - r) / r * 100
            row = {"target": r, "rpm": round(hs["rpm"], 1), "err_pct": round(err, 2),
                   "std": round(hs["std"], 1), "pp": round(hs["pp"], 1),
                   "bus_a": round(hs["a"], 2), "bus_v": round(hs["v"], 1)}
            rows.append(row)
            flag = "  <-- cannot reach target (saturated?)" if err < -2 else ""
            print(f"   {r:4d} rpm: {row['rpm']:7.1f} ({err:+.2f}%) std {row['std']:.1f} "
                  f"pp {row['pp']:.0f}  {row['bus_a']:.2f} A{flag}")
        self.link.cmd("stop")
        self.rec.motor_expected = False
        self.wait_settled_stop()
        self.finish_capture(idx)
        sat = next((r["target"] for r in rows if r["target"] >= 1000 and r["err_pct"] < -2), None)
        self.results["closed_sweep"] = {"rows": rows, "saturates_at_target_rpm": sat}
        if not self.a.yes:
            note = input("  Any rpm where you saw/heard vibration or resonance? (free text, Enter = none) ")
            self.results["closed_sweep"]["operator_note"] = note

    def t_step(self):
        print("\n--- T4 step response ---")
        targets = [t for t in (1000, 2000, 3000, 4000) if t <= self.a.max_rpm]
        accels = [300, 2000]
        res = []
        for target in targets:
            for acc in accels:
                for rep in range(self.a.reps):
                    tag = f"step_{target}_a{acc}_r{rep+1}"
                    print(f"   {tag} ...", end="", flush=True)
                    self.set_closed(target, acc)
                    hold = max(self.a.hold, acc / 1000 * 1.5 + 6)
                    idx = self.capture(tag, ["start"], hold)
                    # slow stop on the host side, record separately
                    self.link.cmd("stop")
                    self.rec.motor_expected = False
                    samples = self.finish_capture(idx)
                    self.wait_settled_stop(35)
                    m = self.step_metrics(samples, target, hold)
                    m.update({"accel_ms": acc, "rep": rep + 1, "tag": tag})
                    res.append(m)
                    if "error" in m:
                        print(" ", m["error"])
                    else:
                        print(f" plateau {m['plateau_rpm']} rpm, rise10-90 "
                              f"{m['rise_10_90_ms']} ms, overshoot {m['overshoot_pct']}%, "
                              f"peak {m['peak_current_a']} A, J~{m.get('inertia_est_kgm2')}")
                    self.wait(2)
        self.results["steps"] = res

    def t_decel(self):
        print("\n--- T5 deceleration / regeneration ---")
        res = []
        for acc in (300, 2000):
            for tgt in (1000, 3000):
                if tgt > self.a.max_rpm:
                    continue
                tag = f"decel_{tgt}_a{acc}"
                print(f"   {tag} ...", end="", flush=True)
                self.set_closed(tgt, 1500)
                self.rec.tag = tag + "_spinup"
                self.link.cmds(["profile=reset", "profile=start", "start"])
                self.rec.motor_expected = True
                self.rec.run_started = time.monotonic()
                self.rec.last_valid_rpm_t = time.monotonic()
                self.wait(6 + tgt / 600)
                self.link.ok(f"accel_ms={acc}")
                # profile starts together with stop => t=0 is the stop command
                self.rec.tag = tag
                idx = self.rec.mark()
                self.link.cmds(["profile=reset", "profile=start", "stop"])
                self.rec.motor_expected = False
                v_before = self.rec.since(max(0, idx - 10))
                t_end = time.monotonic() + max(20, acc / 1000 * 2 + 8)
                while time.monotonic() < t_end:
                    self.rec.check()
                    time.sleep(0.1)
                    cur = self.rec.since(idx)
                    if cur and cur[-1]["t"] > 600 and all(s["rpm"] < 30 for s in cur[-3:]):
                        break
                samples = self.finish_capture(idx)
                m = self.decel_metrics(samples, tgt, v_before)
                m.update({"accel_ms": acc, "target": tgt})
                res.append(m)
                print(f" t90->10 {m.get('t_90_10_ms')} ms, to-stop {m.get('t_stop_ms')} ms, "
                      f"bus V peak {m.get('bus_v_max')} (+{m.get('bus_v_rise')})")
                self.wait_settled_stop(35)
        self.results["decel"] = res

    def decel_metrics(self, samples, tgt, before):
        if len(samples) < 5:
            return {"error": "too few samples"}
        r0 = max(s["rpm"] for s in samples[:5])
        t90 = interp_time(samples, 0.9 * r0, rising=False)
        t10 = interp_time(samples, 0.1 * r0, rising=False)
        tz = next((s["t"] for s in samples if s["rpm"] < 30 and s["t"] > 50), None)
        v0 = mean([s["v"] for s in before]) if before else mean([s["v"] for s in samples[:3]])
        vmax = max(s["v"] for s in samples)
        m = {"start_rpm": round(r0, 0), "t_90_10_ms": round(t10 - t90) if (t10 and t90) else None,
             "t_stop_ms": round(tz) if tz else None,
             "peak_decel_rpm_s": round(min(s["acc"] for s in samples), 0),
             "bus_v_max": vmax, "bus_v_rise": round(vmax - v0, 1),
             "peak_current_a": max(s["a"] for s in samples)}
        if t10 and t90 and t10 > t90:
            m["avg_decel_rpm_s"] = round(0.8 * r0 / ((t10 - t90) / 1000), 0)
        return m

    def t_estop(self):
        print("\n--- T6 emergency-stop (brake) from 3000 rpm ---")
        print("  This slams the driver brake. Needs: lid closed, bus voltage margin, e-stop latch reset after.")
        if not ask("Run the e-stop (brake) test now?", self.a.yes):
            self.results["estop"] = {"skipped": True}
            return
        tgt = min(3000, self.a.max_rpm)
        self.set_closed(tgt, 1500)
        self.link.cmds(["profile=reset", "profile=start", "start"])
        self.rec.motor_expected = True
        self.rec.run_started = time.monotonic()
        self.rec.last_valid_rpm_t = time.monotonic()
        self.wait(6 + tgt / 600)
        self.rec.tag = "estop"
        idx = self.rec.mark()
        self.rec.motor_expected = False
        self.rec.alarm = None
        self.link.cmds(["profile=reset", "profile=start", "estop"])
        t_end = time.monotonic() + 12
        while time.monotonic() < t_end:
            time.sleep(0.1)
            cur = self.rec.since(idx)
            if cur and cur[-1]["t"] > 300 and all(s["rpm"] < 30 for s in cur[-3:]):
                break
        samples = self.finish_capture(idx)
        self.rec.alarm = None
        m = self.decel_metrics(samples, tgt, self.rec.since(max(0, idx - 10)))
        self.results["estop"] = m
        print(f"   stop in {m.get('t_stop_ms')} ms, bus V peak {m.get('bus_v_max')} "
              f"(+{m.get('bus_v_rise')}), peak {m.get('peak_current_a')} A")
        print("  Release/reset the e-stop, then I will send clearfault.")
        if not self.a.yes:
            input("  Press Enter when the e-stop is released and the lid is closed... ")
        for _ in range(5):
            try:
                if self.link.cmd("clearfault") == "OK":
                    break
            except Exception:
                pass
            time.sleep(0.5)
        self.rec.alarm = None

    def t_repeat(self):
        n = self.a.repeat
        print(f"\n--- T7 repeatability: {n} identical recipes ---")
        peak = min(3000, self.a.max_rpm)
        recipe = f"ramp=500,1000;500,3000;{peak},1500;{peak},6000;0,3000"
        self.link.ok("mode=closed_loop")
        rows = []
        for i in range(n):
            tag = f"repeat_{i+1}"
            print(f"   run {i+1}/{n} ...", end="", flush=True)
            self.link.ok("accel_ms=300")
            idx = self.capture(tag, [recipe], 15.0)
            self.rec.motor_expected = False
            samples = self.finish_capture(idx)
            self.wait_settled_stop(20)
            seg = window(samples, 7000, 11000)
            plat = mean([s["rpm"] for s in seg])
            t90 = interp_time(samples, 0.9 * peak, after=4000)
            rows.append({"run": i + 1, "plateau_rpm": round(plat, 1),
                         "plateau_std": round(sd([s["rpm"] for s in seg]), 2),
                         "t90_ms": round(t90) if t90 else None,
                         "peak_current_a": max(s["a"] for s in samples)})
            print(f" plateau {plat:.1f} rpm, t90 {rows[-1]['t90_ms']} ms")
            self.wait(2)
        pl = [r["plateau_rpm"] for r in rows]
        t9 = [r["t90_ms"] for r in rows if r["t90_ms"]]
        self.results["repeat"] = {"recipe": recipe, "rows": rows,
                                  "plateau_mean": round(mean(pl), 2), "plateau_sd": round(sd(pl), 2),
                                  "t90_mean_ms": round(mean(t9), 0) if t9 else None,
                                  "t90_sd_ms": round(sd(t9), 0) if t9 else None}

    def t_thermal(self):
        n = self.a.thermal_cycles
        print(f"\n--- T8 thermal soak: {n} cycles of 3000 rpm / 10 s + 10 s rest ---")
        peak = min(3000, self.a.max_rpm)
        log = []
        temp = ask_float("Heatsink temp now (C):", self.a.yes, None)
        log.append({"cycle": 0, "heatsink_c": temp})
        self.set_closed(peak, 1500)
        for c in range(1, n + 1):
            idx = self.capture(f"thermal_{c}", ["start"], 10.0)
            self.link.cmd("stop")
            self.rec.motor_expected = False
            samples = self.finish_capture(idx)
            self.wait_settled_stop(25)
            self.wait(10)
            a_pl = mean([s["a"] for s in samples[-30:]]) if samples else None
            entry = {"cycle": c, "plateau_a": round(a_pl, 2) if a_pl else None}
            if c % 5 == 0 or c == n:
                entry["heatsink_c"] = ask_float(f"Heatsink temp after cycle {c} (C):", self.a.yes, None)
            log.append(entry)
            print(f"   cycle {c}/{n}: plateau {entry['plateau_a']} A "
                  f"{('heatsink ' + str(entry['heatsink_c'])) if entry.get('heatsink_c') else ''}")
        self.results["thermal"] = log

    TESTS = [("baseline", "t_baseline"), ("open", "t_open_sweep"),
             ("closed", "t_closed_sweep"), ("step", "t_step"),
             ("decel", "t_decel"), ("estop", "t_estop"),
             ("repeat", "t_repeat"), ("thermal", "t_thermal")]

    # ---- driver ------------------------------------------------------------
    def run(self, selected):
        self.rec.start()
        try:
            self.preflight()
            for key, fn in self.TESTS:
                if key not in selected:
                    continue
                self.rec.alarm = None
                if not ask(f"Run test '{key}' now? (motor will spin)", self.a.yes):
                    print(f"   skipped {key}")
                    continue
                if not self.a.yes:
                    print("   Starting in 3 s - hand on the e-stop...")
                    time.sleep(3)
                getattr(self, fn)()
                self.stop_motor()
                self.save()
        except KeyboardInterrupt:
            print("\n!! Ctrl-C: stopping motor")
            self.meta["aborted"] = "keyboard interrupt"
        except Abort as e:
            print(f"\n!! ABORT: {e}")
            self.meta["aborted"] = str(e)
        except TimeoutError as e:
            print(f"\n!! ABORT: link timeout: {e}")
            self.meta["aborted"] = f"link timeout: {e}"
        finally:
            ok = self.stop_motor()
            if not ok:
                print("!! 'stop' failed -> sent estop. Check the machine!")
            self.rec.shutdown()
            self.meta["finished"] = datetime.now().isoformat()
            self.save()
            self.report()

    def save(self):
        with open(os.path.join(self.out, "results.json"), "w") as f:
            json.dump(self.results, f, indent=2)
        with open(os.path.join(self.out, "metadata.json"), "w") as f:
            json.dump(self.meta, f, indent=2)

    # ---- report ---------------------------------------------------------------
    def report(self):
        R = self.results
        L = [f"# Spin coater benchmark - {self.a.label}", "",
             f"Run: {self.meta.get('started')}  |  firmware {self.status0.get('firmware_version')}  "
             f"|  supply {self.supply_v} V  |  pole pairs {self.status0.get('pole_pairs')}", ""]
        if self.meta.get("aborted"):
            L += [f"**ABORTED:** {self.meta['aborted']}", ""]
        inp = self.meta.get("inputs", {})
        L += [f"Chuck+substrate: {inp.get('chuck_plus_substrate_g')} g, P-sv {inp.get('psv_amps')} A, "
              f"ambient {inp.get('ambient_c')} C", ""]
        if "open_sweep" in R:
            o = R["open_sweep"]
            L += ["## Open-loop calibration", "",
                  f"- Slope **{o['rpm_per_raw']} rpm/raw**, intercept {o['intercept_rpm']} rpm, R2 {o['r2']}",
                  f"- Top speed **{o['top_rpm']} rpm** at {o['max_rpm_at_supply_v']} V bus",
                  f"- Ke estimate {o['ke_v_s_rad_est']} V.s/rad", "",
                  "| raw | rpm | std | A | V |", "|---|---|---|---|---|"]
            L += [f"| {r['raw']} | {r['rpm']} | {r['std']} | {r['bus_a']} | {r['bus_v']} |"
                  for r in o["rows"]]
            L.append("")
        if "closed_sweep" in R:
            c = R["closed_sweep"]
            L += ["## Closed-loop accuracy", "",
                  f"Saturates (cannot reach target) from: {c['saturates_at_target_rpm']} rpm", "",
                  "| target | rpm | err % | std | p-p | A |", "|---|---|---|---|---|---|"]
            L += [f"| {r['target']} | {r['rpm']} | {r['err_pct']} | {r['std']} | {r['pp']} | {r['bus_a']} |"
                  for r in c["rows"]]
            if c.get("operator_note"):
                L += ["", f"Operator note: {c['operator_note']}"]
            L.append("")
        if "steps" in R:
            L += ["## Step response (best / typical / worst over repeats)", "",
                  "| target | accel_ms | rise 10-90 ms (min/med/max) | overshoot % max | settle 2% ms (med) | "
                  "peak A | avg accel rpm/s (med) | J est kg.m2 (med) |",
                  "|---|---|---|---|---|---|---|---|"]
            keys = sorted({(m['target_rpm'], m['accel_ms']) for m in R["steps"] if "error" not in m})
            for t, a in keys:
                g = [m for m in R["steps"] if m.get("target_rpm") == t and m.get("accel_ms") == a
                     and "error" not in m]
                rise = [m["rise_10_90_ms"] for m in g if m.get("rise_10_90_ms")]
                js = [m["inertia_est_kgm2"] for m in g if m.get("inertia_est_kgm2")]
                L.append(f"| {t} | {a} | {min(rise):.0f} / {statistics.median(rise):.0f} / {max(rise):.0f} | "
                         f"{max(m['overshoot_pct'] for m in g)} | "
                         f"{statistics.median([m['settle_2pct_ms'] for m in g]):.0f} | "
                         f"{max(m['peak_current_a'] for m in g)} | "
                         f"{statistics.median([m.get('avg_accel_rpm_s', 0) for m in g]):.0f} | "
                         f"{statistics.median(js) if js else 'n/a'} |"
                         if rise else f"| {t} | {a} | n/a | | | | | |")
            fails = [m for m in R["steps"] if "error" in m]
            if fails:
                L += ["", f"{len(fails)} step run(s) failed to reach speed."]
            L.append("")
        if "decel" in R:
            L += ["## Deceleration (slow stop = driver decel time)", "",
                  "| from rpm | accel_ms | 90->10% ms | to stop ms | avg decel rpm/s | bus V peak | V rise |",
                  "|---|---|---|---|---|---|---|"]
            L += [f"| {m.get('start_rpm')} | {m['accel_ms']} | {m.get('t_90_10_ms')} | {m.get('t_stop_ms')} | "
                  f"{m.get('avg_decel_rpm_s')} | {m.get('bus_v_max')} | {m.get('bus_v_rise')} |"
                  for m in R["decel"] if "error" not in m]
            L.append("")
        if "estop" in R and not R["estop"].get("skipped"):
            e = R["estop"]
            L += ["## E-stop (brake)", "",
                  f"- From {e.get('start_rpm')} rpm to stop: **{e.get('t_stop_ms')} ms**",
                  f"- Bus voltage peak {e.get('bus_v_max')} V (rise {e.get('bus_v_rise')} V), "
                  f"peak current {e.get('peak_current_a')} A", ""]
        if "repeat" in R:
            r = R["repeat"]
            L += ["## Repeatability", "",
                  f"- Plateau mean {r['plateau_mean']} rpm, SD **{r['plateau_sd']} rpm**",
                  f"- Time to 90%: mean {r['t90_mean_ms']} ms, SD **{r['t90_sd_ms']} ms**", ""]
        if "thermal" in R:
            L += ["## Thermal", ""]
            L += [f"- cycle {t['cycle']}: heatsink {t.get('heatsink_c')} C, plateau {t.get('plateau_a')} A"
                  for t in R["thermal"] if t.get("heatsink_c") is not None or t.get("plateau_a")]
            L.append("")

        # verdicts
        L += ["## Verdicts", ""]
        v = []
        if "closed_sweep" in R and R["closed_sweep"]["saturates_at_target_rpm"]:
            v.append(f"- Closed loop cannot hold targets >= {R['closed_sweep']['saturates_at_target_rpm']} rpm at this "
                     "bus voltage; set a lower max-rpm in recipes.")
        if "estop" in R and R["estop"].get("bus_v_max") and R["estop"]["bus_v_max"] >= SUPPLY_V_MAX - 1:
            v.append("- E-stop brake pushed the bus within 1 V of the limit: add a brake resistor/TVS or lower voltage.")
        if "decel" in R:
            worst = max((m.get("bus_v_rise") or 0) for m in R["decel"] if "error" not in m)
            if worst >= 3:
                v.append(f"- Deceleration raises bus voltage by up to {worst} V (regen). Lengthen decel time or add a sink.")
        if "steps" in R:
            js = [m["inertia_est_kgm2"] for m in R["steps"] if m.get("inertia_est_kgm2")]
            if js:
                v.append(f"- Estimated total inertia ~ {statistics.median(js):.4f} kg.m2 (+-30%).")
            pk = max((m.get("peak_current_a") or 0) for m in R["steps"] if "error" not in m)
            if pk >= 0.9 * DRIVER_I_MAX:
                v.append(f"- Peak bus current {pk} A is at the driver limit: ramps are current-limited. "
                         "Lower RAMP_MAX_ACCEL_RPM_S to the measured avg accel.")
            av = [m.get("avg_accel_rpm_s") for m in R["steps"] if m.get("accel_ms") == 300 and m.get("avg_accel_rpm_s")]
            if av:
                v.append(f"- Achieved fast-spinup acceleration (accel_ms=300): median {statistics.median(av):.0f} rpm/s "
                         f"(firmware ramp cap {RAMP_MAX_RPM_S}). Set RAMP_MAX_ACCEL_RPM_S <= this.")
        if "repeat" in R and R["repeat"]["plateau_sd"] > 5:
            v.append("- Plateau repeatability SD > 5 rpm: check supply regulation and chuck balance.")
        L += v or ["- No red flags raised by the automatic rules."]
        with open(os.path.join(self.out, "report.md"), "w") as f:
            f.write("\n".join(L) + "\n")
        print("\n" + "\n".join(L))
        self.plots()
        print(f"\nAll data saved in {self.out}")

    def plots(self):
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
        except Exception:
            print("(matplotlib not installed: skipping plots; CSVs are in the run folder)")
            return
        by = {}
        with open(os.path.join(self.out, "samples.csv")) as f:
            for r in csv.DictReader(f):
                by.setdefault(r["tag"], []).append(r)
        for tag in [t for t in by if t.startswith("step_") and t.endswith("_r1")]:
            rows = by[tag]
            t = [float(r["t_ms"]) / 1000 for r in rows]
            fig, ax = plt.subplots(2, 1, sharex=True, figsize=(8, 6))
            ax[0].plot(t, [float(r["rpm"]) for r in rows])
            ax[0].set_ylabel("rpm")
            ax[1].plot(t, [float(r["bus_a"]) for r in rows], label="A")
            ax[1].set_ylabel("bus A")
            ax[1].set_xlabel("s")
            ax[0].set_title(tag)
            fig.tight_layout()
            fig.savefig(os.path.join(self.out, f"{tag}.png"), dpi=110)
            plt.close(fig)


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mac", help="Bluetooth MAC of the 'SpinCoat' ESP32")
    ap.add_argument("--port", help="serial/rfcomm device path, e.g. /dev/rfcomm0")
    ap.add_argument("--simulate", action="store_true", help="use the built-in motor simulator")
    ap.add_argument("--label", default="run", help="e.g. bare-chuck or glass182")
    ap.add_argument("--tests", default="baseline,open,closed,step,decel,estop,repeat",
                    help="comma list from: baseline,open,closed,step,decel,estop,repeat,thermal, or 'all'")
    ap.add_argument("--max-rpm", type=int, default=3800, help="never command above this (default 3800)")
    ap.add_argument("--max-current", type=float, default=15.5, help="abort above this bus current (A); driver rating is 15 A")
    ap.add_argument("--current-limit", type=int, default=15, help="driver current limit written to driver (A)")
    ap.add_argument("--hold", type=float, default=6.0, help="hold seconds per speed point")
    ap.add_argument("--reps", type=int, default=3, help="repeats per step-response point")
    ap.add_argument("--repeat", type=int, default=5, help="runs in repeatability test")
    ap.add_argument("--thermal-cycles", type=int, default=10)
    ap.add_argument("--outdir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "bench_runs"))
    ap.add_argument("--pin", default="1234", help="controller PIN (firmware >= 0.300, default 1234)")
    ap.add_argument("--yes", action="store_true",
                    help="auto-answer checklist/prompts (ONLY for --simulate)")
    args = ap.parse_args()

    if args.yes and not args.simulate:
        sys.exit("--yes is only allowed with --simulate (the checklist exists for a reason).")
    if args.max_rpm > RPM_MAX_CL:
        sys.exit(f"--max-rpm cannot exceed {RPM_MAX_CL}")
    selected = {k for k, _ in Bench.TESTS} if args.tests == "all" else set(args.tests.split(","))
    unknown = selected - {k for k, _ in Bench.TESTS}
    if unknown:
        sys.exit(f"unknown tests: {unknown}")

    if args.simulate:
        link = SimLink()
        if args.yes:
            args.hold = min(args.hold, 3.0)
    elif args.port:
        link = TtyLink(args.port)
    else:
        mac = args.mac or find_mac()
        if not mac:
            sys.exit("Could not find 'SpinCoat'. Pair it first (bluetoothctl: scan on; pair <MAC>), "
                     "then pass --mac AA:BB:CC:DD:EE:FF")
        print(f"Connecting to {mac} ...")
        try:
            link = RfcommLink(mac)
        except OSError as e:
            hint = ""
            if getattr(e, "errno", None) == 97:
                hint = ("\nThis PC has no usable Bluetooth RFCOMM support (no adapter, bluetoothd not running, or "
                        "rfcomm module missing). Try: sudo systemctl start bluetooth; sudo modprobe rfcomm\n"
                        "Or pair the ESP32 with your phone/another PC, or use a USB BT dongle.")
            sys.exit(f"Bluetooth connect failed: {e}. Is it paired, powered, and not used by the phone app?{hint}")

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    outdir = os.path.join(args.outdir, f"{stamp}_{re.sub(r'[^A-Za-z0-9_-]', '', args.label)}")
    os.makedirs(outdir, exist_ok=True)
    print(f"Output: {outdir}")

    # first contact
    try:
        time.sleep(0.3)
        st = link.jcmd("status")
    except Exception as e:
        sys.exit(f"No reply to 'status': {e}")
    if not args.simulate:
        reply = link.cmd(f"auth={args.pin}")
        if "OK" not in str(reply):
            sys.exit(f"auth failed ({reply!r}). Use --pin with the PIN set in the app.")
    b = Bench(args, link, outdir)
    b.status0 = st
    try:
        b.run(selected)
    finally:
        link.close()


if __name__ == "__main__":
    main()
