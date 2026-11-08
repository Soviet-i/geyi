#!/usr/bin/env python3
"""Two-board online smoke test.

After flashing the same firmware to both boards, open COM7/COM8, wait for the
RT-Thread shell, then drive the online module through the `gy_net` shell command.
The shell command only queues an action; gy_ui_tick() executes it in the LVGL
main thread, so this exercises the same path as touch input.

Usage:
  python tools/net_board_test.py --port-a COM7 --port-b COM8
"""
from __future__ import annotations

import argparse
import re
import threading
import time
from pathlib import Path

import serial


ROOT = Path(__file__).resolve().parents[1]
OUTDIR = ROOT / "evidence" / "net" / "20261108" / "board"


class Board:
    def __init__(self, name: str, port: str, baud: int = 1_000_000):
        self.name = name
        self.port = port
        self.lines: list[str] = []
        self.lock = threading.Lock()
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = baud
        self.ser.timeout = 0.1
        self.ser.dsrdtr = False
        self.ser.rtscts = False
        self.ser.open()
        # Opening a CH340 port can pulse DTR/RTS; release them immediately.
        try:
            self.ser.dtr = False
            self.ser.rts = False
        except Exception:
            pass
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()

    def _reader(self) -> None:
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
                line = raw.decode("utf-8", "replace").replace("\r", "")
                with self.lock:
                    self.lines.append(line)

    def text(self) -> str:
        with self.lock:
            return "\n".join(self.lines)

    def wait_for(self, needle: str, timeout: float = 10.0) -> bool:
        end = time.time() + timeout
        while time.time() < end:
            if needle in self.text():
                return True
            time.sleep(0.1)
        return False

    def send(self, command: str) -> None:
        self.ser.write((command + "\n").encode())
        self.ser.flush()

    def state(self, timeout: float = 3.0) -> dict[str, str] | None:
        before = len(self.lines)
        self.send("gy_net state")
        end = time.time() + timeout
        pat = re.compile(r"\[gy_test\] state (.+)")
        while time.time() < end:
            with self.lock:
                lines = self.lines[before:]
            for line in lines:
                m = pat.search(line)
                if m:
                    return dict(re.findall(r"(\w+)=([^\s]+)", m.group(1)))
            time.sleep(0.1)
        return None

    def close(self) -> None:
        try:
            self.ser.close()
        except Exception:
            pass


def require(cond: bool, message: str) -> None:
    if not cond:
        raise SystemExit("FAIL: " + message)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port-a", default="COM7")
    ap.add_argument("--port-b", default="COM8")
    ap.add_argument("--boot-wait", type=float, default=8.0)
    args = ap.parse_args()

    OUTDIR.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    a = Board("A", args.port_a)
    b = Board("B", args.port_b)
    ok = False
    try:
        # Opening the ports resets both boards once; wait for shell + BLE power.
        time.sleep(args.boot_wait)
        require(a.wait_for("msh />", 8), "A shell not ready")
        require(b.wait_for("msh />", 8), "B shell not ready")
        require("net=PASS" in a.text(), "A net selftest missing")
        require("net=PASS" in b.text(), "B net selftest missing")

        a.send("gy_net host")
        time.sleep(1.0)
        sa = a.state()
        require(sa is not None, "A state after host missing")
        require(sa.get("active") == "1" and sa.get("players") == "1",
                f"A host state wrong: {sa}")

        b.send("gy_net lobby")
        time.sleep(3.0)
        sb = b.state()
        require(sb is not None, "B state after lobby missing")
        require(int(sb.get("rooms", "0")) >= 1,
                f"B did not discover A's room: {sb}")

        b.send("gy_net join 0")
        time.sleep(6.0)
        sb = b.state()
        sa = a.state()
        require(sb is not None and sb.get("active") == "2",
                f"B join failed: {sb}")
        require(sb.get("role") == "1" and sb.get("local") == "1",
                f"B is not blue player: {sb}")
        require(sa is not None and sa.get("players") == "2",
                f"A did not see guest: {sa}")

        a.send("gy_net ready")
        time.sleep(0.5)
        b.send("gy_net ready")
        time.sleep(4.0)
        sa = a.state()
        sb = b.state()
        require(sa is not None and sa.get("phase") == "2",
                f"A not playing: {sa}")
        require(sb is not None and sb.get("phase") == "2",
                f"B not playing: {sb}")

        seq0 = int(sa.get("seq", "0"))
        a.send("gy_net move 0 1")
        time.sleep(1.5)
        sa = a.state()
        sb = b.state()
        require(sa is not None and int(sa.get("seq", "0")) > seq0,
                f"A move did not advance seq: {sa}")
        require(sb is not None and sb.get("turn") == "1",
                f"B did not receive A snapshot: {sb}")

        seq1 = int(sb.get("seq", "0"))
        b.send("gy_net move 2 1")
        time.sleep(1.5)
        sa = a.state()
        sb = b.state()
        require(sb is not None and int(sb.get("seq", "0")) > seq1,
                f"B move did not advance seq: {sb}")
        require(sa is not None and sa.get("turn") == "0",
                f"A did not receive B snapshot: {sa}")

        atext, btext = a.text(), b.text()
        for needle in ("adv started", "scan started", "connected", "cccd",
                       "remote svc registered"):
            require(needle in atext or needle in btext,
                    f"missing log event: {needle}")
        ok = True
        print("PASS: two-board online smoke")
        print("A final:", sa)
        print("B final:", sb)
    finally:
        (OUTDIR / f"auto_A_{args.port_a}_{stamp}.log").write_text(a.text(), encoding="utf-8")
        (OUTDIR / f"auto_B_{args.port_b}_{stamp}.log").write_text(b.text(), encoding="utf-8")
        a.close()
        b.close()
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
