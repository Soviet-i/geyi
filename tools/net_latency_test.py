#!/usr/bin/env python3
"""Measure online-room latency on two real boards.

This is the reproduction test for the student's 2026-11-09 complaint:
host close and guest join felt like several seconds of frozen UI.

It drives the same `gy_net` shell path as touch, polls `state`, and fails when:
  - B does not discover A's room within 2.5 s;
  - A/B do not both see players=2 within 2.5 s of B pressing join;
  - A does not see players=1 within 1.5 s of B leaving;
  - B's lobby does not drop a closed room within 2.0 s of A leaving.

Usage:
  python tools/net_latency_test.py --port-a COM7 --port-b COM8
"""
from __future__ import annotations

import argparse
import re
import threading
import time
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parents[1]
OUTDIR = ROOT / "evidence" / "net" / "20261109" / "latency"


class Board:
    def __init__(self, name: str, port: str):
        self.name = name
        self.lines: list[str] = []
        self.lock = threading.Lock()
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = 1_000_000
        self.ser.timeout = 0.1
        self.ser.dsrdtr = False
        self.ser.rtscts = False
        self.ser.open()
        self.ser.dtr = False
        self.ser.rts = False
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        buf = b""
        while True:
            try:
                data = self.ser.read(4096)
            except Exception:
                return
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                with self.lock:
                    self.lines.append(raw.decode("utf-8", "replace").replace("\r", ""))

    def text(self):
        with self.lock:
            return "\n".join(self.lines)

    def send(self, cmd):
        self.ser.write((cmd + "\n").encode())
        self.ser.flush()
        time.sleep(0.05)

    def state(self, timeout=1.5):
        before = len(self.lines)
        self.send("gy_net state")
        end = time.time() + timeout
        merged = {}
        pat = re.compile(r"\[gy_test\] (?:state|info|ready) (.+)")
        while time.time() < end:
            with self.lock:
                lines = self.lines[before:]
            for line in lines:
                if "[gy_test] end" in line:
                    return merged or None
                m = pat.search(line)
                if m:
                    merged.update(re.findall(r"(\w+)=([^\s]+)", m.group(1)))
            time.sleep(0.04)
        return merged or None

    def wait(self, pred, timeout, what):
        start = time.time()
        last = None
        while time.time() - start < timeout:
            last = self.state()
            if last and pred(last):
                return time.time() - start, last
            time.sleep(0.1)
        raise RuntimeError(f"{what} timeout ({timeout}s), last={last}")

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


def reset(a, b):
    a.send("gy_net lobby")
    b.send("gy_net lobby")
    time.sleep(1.5)


def measure(a, b):
    results = {}
    reset(a, b)

    t0 = time.time()
    a.send("gy_net host")
    dt, _ = a.wait(lambda s: s.get("active") == "1", 4, "A host")
    b.send("gy_net lobby")
    dt, _ = b.wait(lambda s: int(s.get("rooms", "0")) >= 1, 8, "B discover")
    results["discover"] = time.time() - t0

    t0 = time.time()
    b.send("gy_net join 0")
    _, sb = b.wait(lambda s: s.get("active") == "2" and s.get("local") == "1", 10, "B join")
    _, sa = a.wait(lambda s: s.get("players") == "2", 10, "A sees guest")
    results["join"] = time.time() - t0

    t0 = time.time()
    b.send("gy_net leave")
    b.wait(lambda s: s.get("active") == "0", 5, "B leave")
    _, sa = a.wait(lambda s: s.get("players") == "1", 5, "A sees leave")
    results["leave_visible"] = time.time() - t0

    b.send("gy_net lobby")
    b.wait(lambda s: int(s.get("rooms", "0")) >= 1, 6, "B rediscovers room")
    t0 = time.time()
    a.send("gy_net leave")
    b.wait(lambda s: int(s.get("rooms", "0")) == 0, 8, "B sees close")
    results["close_visible"] = time.time() - t0
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port-a", default="COM7")
    ap.add_argument("--port-b", default="COM8")
    args = ap.parse_args()
    OUTDIR.mkdir(parents=True, exist_ok=True)
    a = Board("A", args.port_a)
    b = Board("B", args.port_b)
    try:
        time.sleep(7)
        if not a.text().strip() or not b.text().strip():
            raise RuntimeError("boards not booted")
        r = measure(a, b)
        for k, v in r.items():
            print(f"{k}: {v:.2f}s")
        limits = {"discover": 2.5, "join": 2.5, "leave_visible": 1.5, "close_visible": 2.0}
        bad = [f"{k}={r[k]:.2f}s > {limits[k]:.1f}s" for k in limits if r[k] > limits[k]]
        if bad:
            print("FAIL: " + "; ".join(bad))
            return 1
        print("PASS: latency budget")
        return 0
    finally:
        stamp = time.strftime("%Y%m%d_%H%M%S")
        (OUTDIR / f"latency_A_{args.port_a}_{stamp}.log").write_text(a.text(), encoding="utf-8")
        (OUTDIR / f"latency_B_{args.port_b}_{stamp}.log").write_text(b.text(), encoding="utf-8")
        a.close()
        b.close()


if __name__ == "__main__":
    raise SystemExit(main())
