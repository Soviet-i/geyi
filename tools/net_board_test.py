#!/usr/bin/env python3
"""Two-board online acceptance suite.

Drives the real firmware through the `gy_net` shell command.  The command only
queues an action; gy_ui_tick() executes it in the LVGL main thread, so this
exercises the same code path as touch input.

Coverage:
  1. boot self-test and unique player IDs (chip UID derived);
  2. create / discover / join, per-seat ready flags;
  3. a complete game driven by `gy_net suggest` until RESULT;
  4. guest leave -> host sees 1/2 immediately; rejoin is a player again;
  5. host leave -> room disappears from the other board's lobby;
  6. clicking a room whose host has gone does not make the client a host;
  7. forced spectator join, read-only, and immediate leave.

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
OUTDIR = ROOT / "evidence" / "net" / "20261109" / "board"


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
        time.sleep(0.05)

    def state(self, timeout: float = 4.0) -> dict[str, str] | None:
        before = len(self.lines)
        self.send("gy_net state")
        end = time.time() + timeout
        pat = re.compile(r"\[gy_test\] (?:state|info|ready) (.+)")
        merged: dict[str, str] = {}
        while time.time() < end:
            with self.lock:
                lines = self.lines[before:]
            for line in lines:
                if "[gy_test] end" in line:
                    return merged or None
                m = pat.search(line)
                if m:
                    merged.update(re.findall(r"(\w+)=([^\s]+)", m.group(1)))
            time.sleep(0.05)
        return merged or None

    def suggest(self, timeout: float = 2.0) -> tuple[str, ...] | None:
        before = len(self.lines)
        self.send("gy_net suggest")
        end = time.time() + timeout
        pat = re.compile(r"\[gy_test\] suggest (move \d+ \d+|place \d+|none)")
        result = None
        while time.time() < end:
            with self.lock:
                lines = self.lines[before:]
            for line in lines:
                m = pat.search(line)
                if m:
                    result = tuple(m.group(1).split())
                if "[gy_test] end" in line:
                    return result
            time.sleep(0.05)
        return result

    def view(self, timeout: float = 2.0) -> dict[str, str] | None:
        before = len(self.lines)
        self.send("gy_net view")
        end = time.time() + timeout
        out: dict[str, str] = {}
        pat = re.compile(r"\[gy_test\] view (\w+)=(.*)")
        while time.time() < end:
            with self.lock:
                lines = self.lines[before:]
            for line in lines:
                m = pat.search(line)
                if m:
                    out[m.group(1)] = m.group(2).strip()
                if "[gy_test] end" in line:
                    return out
            time.sleep(0.05)
        return out or None

    def close(self) -> None:
        try:
            self.ser.close()
        except Exception:
            pass


def require(cond: bool, message: str) -> None:
    if not cond:
        raise SystemExit("FAIL: " + message)


def wait_state(board: Board, pred, timeout: float, what: str) -> dict[str, str]:
    end = time.time() + timeout
    last = None
    while time.time() < end:
        last = board.state()
        if last is not None and pred(last):
            return last
        time.sleep(0.25)
    raise SystemExit(f"FAIL: {board.name} {what} timeout; last={last}")


def reset(a: Board, b: Board) -> None:
    a.send("gy_net lobby")
    b.send("gy_net lobby")
    wait_state(a, lambda s: s.get("active") == "0", 5, "A reset to lobby")
    wait_state(b, lambda s: s.get("active") == "0", 5, "B reset to lobby")
    time.sleep(1.2)


def create_and_join(a: Board, b: Board) -> tuple[dict[str, str], dict[str, str]]:
    for attempt in range(3):
        a.send("gy_net host")
        try:
            sa = wait_state(a, lambda s: s.get("active") == "1" and
                            s.get("players") == "1", 4, "host create")
            break
        except SystemExit:
            if attempt == 2:
                raise
            time.sleep(0.5)
    b.send("gy_net lobby")
    wait_state(b, lambda s: int(s.get("rooms", "0")) >= 1, 6, "discover room")
    joined = False
    for attempt in range(3):
        b.send("gy_net join 0")
        try:
            sb = wait_state(b, lambda s: s.get("active") == "2" and s.get("role") == "1" and
                            s.get("local") == "1", 10, "join as player")
            joined = True
            break
        except SystemExit:
            if attempt == 2:
                raise
            b.send("gy_net lobby")
            time.sleep(1.0)
    require(joined, "player join failed")
    sa = wait_state(a, lambda s: s.get("players") == "2", 4, "see guest")
    return sa, sb


def test_boot_and_ids(a: Board, b: Board) -> None:
    require("net=PASS" in a.text(), "A net self-test missing")
    require("net=PASS" in b.text(), "B net self-test missing")
    sa = wait_state(a, lambda s: True, 10, "A boot state")
    sb = wait_state(b, lambda s: True, 10, "B boot state")
    ida, idb = int(sa["id"], 16), int(sb["id"], 16)
    require(ida != 0 and idb != 0, f"player ID is zero: A={ida:04X} B={idb:04X}")
    require(ida != idb, f"player IDs are not unique: A={ida:04X} B={idb:04X}")


def view_seat(v: dict[str, str], key: str) -> tuple[str, str]:
    name, _, state = v.get(key, "").partition("|")
    return name, state


def test_empty_seat_ui(a: Board, b: Board) -> None:
    reset(a, b)
    a.send("gy_net host")
    wait_state(a, lambda s: s.get("active") == "1", 4, "host for empty-seat UI")
    v = a.view()
    require(v is not None, "A view missing")
    red, rstate = view_seat(v, "red")
    blue, bstate = view_seat(v, "blue")
    require("点击交换" in blue and bstate == "", f"empty seat text wrong: {v}")
    require("空位" not in (red + blue), f"empty seat still says 空位: {v}")
    require("(房主)" in red, f"host name missing (房主): {v}")
    require("(对手)" not in (red + blue), f"unexpected (对手): {v}")
    a.send("gy_net leave")


def test_join_ids_and_ready(a: Board, b: Board) -> None:
    sa, sb = create_and_join(a, b)
    ida, idb = int(sa["id"], 16), int(sb["id"], 16)
    v = a.view()
    require(v is not None, "A room view missing")
    red, rstate = view_seat(v, "red")
    blue, bstate = view_seat(v, "blue")
    require(f"#{ida:04X}" in red and "(房主)" in red, f"A host seat text: {v}")
    require(f"#{idb:04X}" in blue and "(对手)" not in blue, f"A guest seat text: {v}")
    require(rstate == "未准备" and bstate == "未准备", f"initial ready text: {v}")
    require(int(sa["hostid"], 16) == ida, f"A host id wrong: {sa}")
    require(int(sa["guestid"], 16) == idb, f"A guest id wrong: {sa}")
    require(int(sb["hostid"], 16) == ida, f"B does not see A's id: {sb}")
    require(int(sb["guestid"], 16) == idb, f"B does not see its own id: {sb}")

    # Only the player who pressed ready may become ready.
    a.send("gy_net ready")
    sa = wait_state(a, lambda s: s.get("hready") == "1" and s.get("gready") == "0", 3,
                    "host ready")
    sb = wait_state(b, lambda s: s.get("hready") == "1" and s.get("gready") == "0", 3,
                    "see host ready")
    require(sa.get("rready") == "1" and sa.get("bready") == "0", f"A ready flags: {sa}")
    require(sb.get("rready") == "1" and sb.get("bready") == "0", f"B ready flags: {sb}")
    v = a.view()
    require(v is not None, "A view after host ready missing")
    _, rstate = view_seat(v, "red")
    _, bstate = view_seat(v, "blue")
    require(rstate == "已准备" and bstate == "未准备",
            f"only host seat should be ready: {v}")

    b.send("gy_net ready")
    # STARTING 的 3 秒窗口很短，先验证倒计时字段确实在 3/2/1 变化。
    wait_state(a, lambda s: s.get("phase") == "1" and s.get("cd") in ("3", "2", "1"),
               4, "countdown visible")
    sa = wait_state(a, lambda s: s.get("phase") == "2", 6, "both ready -> playing")
    sb = wait_state(b, lambda s: s.get("phase") == "2", 6, "B sees playing")
    # 开局时 ready 清零，下一局需要重新准备。
    require(sa.get("hready") == "0" and sa.get("gready") == "0",
            f"A ready not cleared at start: {sa}")
    require(sb.get("hready") == "0" and sb.get("gready") == "0",
            f"B ready not cleared at start: {sb}")


def test_full_game(a: Board, b: Board) -> None:
    for _ in range(120):
        sa = a.state()
        require(sa is not None, "A state missing during game")
        if sa.get("phase") == "3":
            break
        require(sa.get("phase") == "2", f"A left playing unexpectedly: {sa}")
        turn = int(sa["turn"])
        side = a if int(sa["local"]) == turn else b
        before = int(side.state()["seq"])  # type: ignore[index]
        cmd = side.suggest()
        require(cmd is not None and cmd[0] != "none", f"{side.name} has no legal action")
        side.send("gy_net " + " ".join(cmd))
        wait_state(side, lambda s, n=before: int(s.get("seq", "0")) > n, 4,
                   "local/remote seq advance")
        time.sleep(0.35)
    else:
        raise SystemExit("FAIL: game did not finish in 120 turns")

    sa = wait_state(a, lambda s: s.get("phase") == "3", 4, "A result")
    sb = wait_state(b, lambda s: s.get("phase") == "3", 6, "B result")
    require(sa.get("outcome") != "0", f"A result has no winner: {sa}")
    require(sb.get("result") == sa.get("result"), f"result mismatch A={sa} B={sb}")

    # 结算后房间必须保留并回到 WAIT，可以再开一局；不能直接消失。
    sa = wait_state(a, lambda s: s.get("phase") == "0" and s.get("players") == "2",
                    7, "A back to room")
    sb = wait_state(b, lambda s: s.get("phase") == "0" and s.get("players") == "2",
                    7, "B back to room")
    require(sa.get("active") == "1" and sb.get("active") == "2", f"room vanished: {sa} {sb}")
    a.send("gy_net ready")
    b.send("gy_net ready")
    sa = wait_state(a, lambda s: s.get("phase") == "2", 8, "A second game")
    sb = wait_state(b, lambda s: s.get("phase") == "2", 8, "B second game")
    require(sa.get("players") == "2" and sb.get("players") == "2",
            f"second game did not start: {sa} {sb}")


def test_leave_rejoin_lifecycle(a: Board, b: Board) -> None:
    reset(a, b)
    create_and_join(a, b)
    b.send("gy_net leave")
    wait_state(b, lambda s: s.get("active") == "0", 4, "B leave")
    # Host must publish 1/2 immediately, not after the 10 s window.
    wait_state(a, lambda s: s.get("players") == "1" and s.get("phase") == "0", 3,
               "host sees guest leave")

    b.send("gy_net lobby")
    wait_state(b, lambda s: int(s.get("rooms", "0")) >= 1, 6, "rediscover after leave")
    b.send("gy_net join 0")
    # The room is 1/2 again, so this must be a player seat, not a spectator.
    sb = wait_state(b, lambda s: s.get("active") == "2" and s.get("role") == "1" and
                    s.get("local") == "1", 10, "rejoin as player")
    wait_state(a, lambda s: s.get("players") == "2", 4, "host sees rejoin")
    require(int(sb["local"]) == 1, f"rejoin became spectator: {sb}")

    b.send("gy_net leave")
    wait_state(b, lambda s: s.get("active") == "0", 4, "B second leave")
    wait_state(a, lambda s: s.get("players") == "1", 3, "host sees second leave")
    a.send("gy_net leave")
    b.send("gy_net lobby")
    wait_state(b, lambda s: int(s.get("rooms", "0")) == 0, 6,
               "closed room disappears from lobby")


def test_host_transfer(a: Board, b: Board) -> None:
    reset(a, b)
    create_and_join(a, b)
    idb = int(wait_state(b, lambda s: True, 3, "B id").get("id", "0"), 16)
    a.send("gy_net leave")
    # Two-player room: the guest takes over instead of the room disappearing.
    sb = wait_state(b, lambda s: s.get("active") == "1" and s.get("role") == "0" and
                    s.get("players") == "1", 8, "guest promoted to host")
    # hostid/guestid are actually red/blue seat IDs; the promoted host may be
    # blue, so look up the seat that now holds host_color.
    seat_key = "guestid" if sb.get("host") == "1" else "hostid"
    require(int(sb[seat_key], 16) == idb, f"promoted host id wrong: {sb}")
    require(sb.get("phase") == "0", f"promoted room should wait: {sb}")
    # Now the sole host leaves: room must disappear for everyone.
    b.send("gy_net leave")
    b.send("gy_net lobby")
    wait_state(b, lambda s: s.get("active") == "0", 4, "B promoted host leave")
    wait_state(b, lambda s: int(s.get("rooms", "0")) == 0, 6,
               "promoted room disappears")


def test_stale_room_click(a: Board, b: Board) -> None:
    reset(a, b)
    a.send("gy_net host")
    wait_state(a, lambda s: s.get("active") == "1", 4, "host for stale test")
    b.send("gy_net lobby")
    wait_state(b, lambda s: int(s.get("rooms", "0")) >= 1, 6, "discover stale room")
    a.send("gy_net leave")
    # Click almost immediately, while the summary may still be cached.
    time.sleep(0.2)
    b.send("gy_net join 0")
    sb = wait_state(b, lambda s: s.get("active") == "0", 8, "stale join returns to lobby")
    require(sb.get("role") != "0", f"stale room made B a host: {sb}")
    require(sb.get("joinreason") == "1", f"stale join did not report room-not-found: {sb}")
    # The room must be gone from the lobby, and no half-open join may remain.
    wait_state(b, lambda s: int(s.get("rooms", "0")) == 0, 6, "stale room removed")
    sb = b.state()
    require(sb is not None and sb.get("active") == "0", f"B stuck joining: {sb}")


def test_spectator(a: Board, b: Board) -> None:
    reset(a, b)
    a.send("gy_net host")
    wait_state(a, lambda s: s.get("active") == "1", 4, "host for spectator")
    b.send("gy_net lobby")
    wait_state(b, lambda s: int(s.get("rooms", "0")) >= 1, 6, "discover spectator room")
    b.send("gy_net watch 0")
    sb = wait_state(b, lambda s: s.get("active") == "2" and s.get("role") == "2" and
                    s.get("local") == "255", 10, "join as spectator")
    sa = wait_state(a, lambda s: s.get("spectators") == "1" and s.get("players") == "1",
                    4, "host sees spectator")
    require(sb.get("role") == "2", f"watch did not become spectator: {sb}")
    require(sa.get("players") == "1", f"spectator occupied a player seat: {sa}")
    vb = b.view()
    require(vb is not None and vb.get("readybtn") == "0",
            f"spectator still shows ready button: {vb}")
    require(vb is not None and "旁观" in vb.get("hint", ""),
            f"spectator hint wrong: {vb}")

    b.send("gy_net move 0 1")   # must be rejected: spectators are read-only
    time.sleep(0.8)
    sb2 = b.state()
    require(sb2 is not None and sb2.get("role") == "2", f"spectator changed role: {sb2}")
    require(sb2.get("phase") == "0", f"spectator changed room phase: {sb2}")

    b.send("gy_net leave")
    wait_state(a, lambda s: s.get("spectators") == "0", 4, "host sees spectator leave")
    a.send("gy_net leave")
    wait_state(b, lambda s: int(s.get("rooms", "0")) == 0, 6,
               "final closed room disappears")


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
        time.sleep(args.boot_wait)
        require(a.wait_for("msh />", 8), "A shell not ready")
        require(b.wait_for("msh />", 8), "B shell not ready")
        require(a.wait_for("[geyi:ble] local id=", 12), "A local ID log missing")
        require(b.wait_for("[geyi:ble] local id=", 12), "B local ID log missing")
        # The shell prompt appears before LVGL/board init finishes; wait until
        # gy_ui_tick() is actually servicing commands.
        require(a.wait_for("[geyi:board] Keep board still", 15), "A UI not ready")
        require(b.wait_for("[geyi:board] Keep board still", 15), "B UI not ready")
        time.sleep(1.0)

        print("== boot / unique ID ==")
        test_boot_and_ids(a, b)
        print("== empty seat UI ==")
        test_empty_seat_ui(a, b)
        print("== join / ready ==")
        test_join_ids_and_ready(a, b)
        print("== full game ==")
        test_full_game(a, b)
        print("== leave / rejoin / close ==")
        test_leave_rejoin_lifecycle(a, b)
        print("== host transfer ==")
        test_host_transfer(a, b)
        print("== stale room click ==")
        test_stale_room_click(a, b)
        print("== spectator ==")
        test_spectator(a, b)

        ok = True
        print("PASS: two-board online suite")
        print("A final:", a.state())
        print("B final:", b.state())
    finally:
        (OUTDIR / f"suite_A_{args.port_a}_{stamp}.log").write_text(a.text(), encoding="utf-8")
        (OUTDIR / f"suite_B_{args.port_b}_{stamp}.log").write_text(b.text(), encoding="utf-8")
        a.close()
        b.close()
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
