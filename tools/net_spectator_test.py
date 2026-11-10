#!/usr/bin/env python3
"""Three-board spectator acceptance suite.

Independent of the long two-board suite: this script focuses on the third
device watching one host + one guest.  It reuses the Board/state/view helpers
from net_board_test.py, but does not run its 15 sections.

Flow covered:
  room page -> ready -> game -> result -> room page -> second game;
  spectator read-only / preview routing / mid-game join / leave;
  guest reboot in WAIT and PLAYING; spectator must see guest offline, then
  guest rejoin or timeout result; spectator transient link drop and REJOIN.

Usage:
  python tools/net_spectator_test.py --port-a COM7 --port-b COM8 --port-c COM5
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from net_board_test import Board, lobby_and_join, reboot_board, wait_state  # noqa: E402

OUTDIR = ROOT / "evidence" / "net" / time.strftime("%Y%m%d") / "spectator"


def require(cond: bool, message: str) -> None:
    if not cond:
        raise SystemExit("FAIL: " + message)


def state(board: Board, timeout: float = 4.0) -> dict[str, str]:
    s = board.state(timeout)
    require(s is not None, f"{board.name} state missing")
    return s  # type: ignore[return-value]


def view(board: Board, timeout: float = 3.0) -> dict[str, str]:
    v = board.view(timeout)
    require(v is not None, f"{board.name} view missing")
    return v  # type: ignore[return-value]


def prev(s: dict[str, str], key: str) -> tuple[int, int, int]:
    parts = s.get(key, "0,-1,0").split(",")
    require(len(parts) == 3, f"bad {key}={s.get(key)}")
    return int(parts[0]), int(parts[1]), int(parts[2])


def wait_view(board: Board, pred, timeout: float, what: str) -> dict[str, str]:
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            last = board.view()
        except Exception:
            last = None
        if last is not None and pred(last):
            return last
        time.sleep(0.15)
    raise SystemExit(f"FAIL: {board.name} {what} timeout; last={last}")


def reset_all(a: Board, b: Board, c: Board) -> None:
    for board in (a, b, c):
        board.send("gy_net lobby")
    for board in (a, b, c):
        wait_state(board, lambda s: s.get("active") == "0", 6, "reset to lobby")
    time.sleep(1.2)


def boot(board: Board) -> None:
    # 若串口在板子已经启动后才打开，历史日志看不到；先敲一次空行让 msh 重新打印提示符。
    board.send("")
    require(board.wait_for("msh />", 8), f"{board.name} shell missing")
    require(board.wait_for("[geyi:ble] local id=", 12), f"{board.name} BLE id missing")
    require(board.wait_for("[geyi:lcd] READY", 15), f"{board.name} UI not ready")
    time.sleep(1.0)


def setup_room(a: Board, b: Board, c: Board) -> None:
    reset_all(a, b, c)
    a.send("gy_net host")
    wait_state(a, lambda s: s.get("active") == "1" and s.get("players") == "1",
               5, "host room")
    b.send("gy_net lobby")
    wait_state(b, lambda s: int(s.get("rooms", "0")) >= 1, 6, "B discover")
    b.send("gy_net join 0")
    wait_state(b, lambda s: s.get("active") == "2" and s.get("role") == "1" and
               s.get("local") == "1", 10, "B join player")
    wait_state(a, lambda s: s.get("players") == "2", 5, "host sees guest")

    c.send("gy_net lobby")
    wait_state(c, lambda s: int(s.get("rooms", "0")) >= 1, 6, "C discover")
    c.send("gy_net watch 0")
    wait_state(c, lambda s: s.get("active") == "2" and s.get("role") == "2" and
               s.get("local") == "255", 10, "C join spectator")
    wait_state(a, lambda s: s.get("spectators") == "1" and s.get("players") == "2",
               5, "host sees spectator")
    wait_state(b, lambda s: s.get("spectators") == "1", 5, "guest sees spectator")
    sc = state(c)
    require(sc.get("spec") == "1" and sc.get("players") == "2" and
            sc.get("spectators") == "1", f"C not spectator: {sc}")
    require(view(c).get("readybtn") == "0", "spectator has ready button")
    require(view(c).get("playing") == "0", "spectator not on room page")


def test_readonly(a: Board, b: Board, c: Board) -> None:
    for cmd in ("ready", "move 0 1", "place 0", "resign"):
        c.send("gy_net " + cmd)
        time.sleep(0.35)
    sc = state(c)
    sa = state(a)
    sb = state(b)
    require(sc.get("role") == "2" and sc.get("phase") == "0", f"spectator changed: {sc}")
    require(sc.get("spectators") == "1" and sc.get("players") == "2",
            f"spectator affected room: {sc}")
    require(sa.get("phase") == "0" and sb.get("phase") == "0",
            f"read-only command changed phase: A={sa} B={sb}")
    require(view(c).get("readybtn") == "0", "spectator ready button appeared")


def start_game(a: Board, b: Board, c: Board) -> None:
    a.send("gy_net ready")
    b.send("gy_net ready")
    wait_state(a, lambda s: s.get("phase") in ("1", "2"), 5, "A countdown")
    wait_state(b, lambda s: s.get("phase") in ("1", "2"), 5, "B countdown")
    wait_state(a, lambda s: s.get("phase") == "2", 8, "A playing")
    wait_state(b, lambda s: s.get("phase") == "2", 8, "B playing")
    wait_state(c, lambda s: s.get("phase") == "2", 8, "C playing")
    require(view(c).get("playing") == "1", "spectator did not enter game page")
    require(state(c).get("spectators") == "1", "spectator disappeared at start")


def test_preview_and_sync(a: Board, b: Board, c: Board) -> None:
    sa = state(a)
    turn = int(sa["turn"])
    side = a if int(sa["local"]) == turn else b
    ss = state(side)
    local = int(ss["local"])
    cmd = side.suggest()
    require(cmd is not None and cmd[0] == "move", f"no move for preview: {cmd}")
    direction, count = int(cmd[1]), int(cmd[2])
    side.send(f"gy_net preview {direction} {count}")
    key = "prevr" if local == 0 else "prevb"
    end = time.time() + 4
    got = None
    while time.time() < end:
        got = prev(state(c), key)
        if got[0] == 1:
            break
        time.sleep(0.15)
    require(got is not None and got[0] == 1, f"spectator missing {key}: {state(c)}")
    require(got[1:] == (direction, count), f"spectator preview wrong: {got}")

    side.send("gy_net cancel")
    end = time.time() + 4
    while time.time() < end:
        got = prev(state(c), key)
        if got[0] == 0:
            break
        time.sleep(0.15)
    require(got is not None and got[0] == 0, f"spectator preview not cleared: {got}")

    before = int(state(a)["seq"])
    side.send(f"gy_net move {direction} {count}")
    wait_state(a, lambda s, n=before: int(s.get("seq", "0")) > n, 5, "seq advance")
    wait_state(c, lambda s, n=before: int(s.get("seq", "0")) > n, 5, "C seq advance")
    time.sleep(0.4)
    sa2 = state(a)
    sc2 = state(c)
    require(sa2.get("turn") == sc2.get("turn") and
            sa2.get("rev") == sc2.get("rev"),
            f"spectator board out of sync: A={sa2} C={sc2}")

    # 换手后另一方也能把预览路由到旁观者。
    side = b if side is a else a
    turn = int(state(a)["turn"])
    require(int(state(side)["local"]) == turn,
            f"second-side turn mismatch: A={state(a)} side={state(side)}")
    cmd = side.suggest()
    require(cmd is not None and cmd[0] == "move", f"no second-side move: {cmd}")
    d, n = int(cmd[1]), int(cmd[2])
    side.send(f"gy_net preview {d} {n}")
    key = "prevr" if int(state(side)["local"]) == 0 else "prevb"
    end = time.time() + 4
    got = None
    while time.time() < end:
        got = prev(state(c), key)
        if got[0] == 1:
            break
        time.sleep(0.15)
    require(got is not None and got[0] == 1,
            f"second-side preview missing: {state(c)}")
    side.send("gy_net cancel")


def test_result_cycle(a: Board, b: Board, c: Board) -> None:
    # 先让当前玩家保留一个旁观可见的预览，再结算；验证 RESULT 确实清掉它，
    # 而不是因为前面已经 cancel 才看到 0。
    sa0 = state(a)
    turn = int(sa0["turn"])
    side = a if int(sa0["local"]) == turn else b
    cmd = side.suggest()
    require(cmd is not None and cmd[0] == "move", f"no move to preview: {cmd}")
    direction, count = int(cmd[1]), int(cmd[2])
    side.send(f"gy_net preview {direction} {count}")
    key = "prevr" if int(state(side)["local"]) == 0 else "prevb"
    wait_state(c, lambda s: prev(s, key)[0] == 1, 4,
               "spectator preview before result")
    a.send("gy_net resign")
    wait_state(a, lambda s: s.get("phase") == "3", 5, "A result")
    wait_state(c, lambda s: s.get("phase") == "3", 5, "C result")
    vc = view(c)
    require(vc.get("result", "") != "", f"spectator result text missing: {vc}")
    wait_state(a, lambda s: s.get("phase") == "0" and s.get("players") == "2",
               8, "A back room")
    wait_state(c, lambda s: s.get("phase") == "0" and s.get("players") == "2",
               8, "C back room")
    require(view(c).get("playing") == "0", "spectator did not return to room page")
    require(state(c).get("spectators") == "1", "spectator lost after result")
    sc = state(c)
    require(prev(sc, "prevr")[0] == 0 and prev(sc, "prevb")[0] == 0,
            f"spectator kept stale preview after result: {sc}")

    a.send("gy_net ready")
    b.send("gy_net ready")
    wait_state(c, lambda s: s.get("phase") == "2", 8, "C second game")
    require(view(c).get("playing") == "1", "spectator did not re-enter second game")


def test_guest_reboot_wait(a: Board, b: Board, c: Board) -> None:
    setup_room(a, b, c)
    amark = a.mark()
    reboot_board(b)
    sa = wait_state(a, lambda s: s.get("peer") == "0" and
                    int(s.get("reconn", "0")) > 0, 8, "host WAIT window")
    sc = wait_state(c, lambda s: s.get("seatb") == "0" and
                     int(s.get("secb", "0")) > 0, 8, "spectator WAIT window")
    require(sa.get("players") == "2", f"host released WAIT seat: {sa}")
    require(sc.get("off") == "1" and sc.get("spec") == "1",
            f"spectator did not see blue offline: {sc}")
    first_sec = int(sc.get("secb", "0"))
    require(first_sec > 0, f"spectator has no countdown: {sc}")
    wait_state(c, lambda s: 0 < int(s.get("secb", "0")) < first_sec and
               s.get("players") == "2" and s.get("seatb") == "0", 4,
               "spectator countdown decreases while seat reserved")
    require("蓝方" in view(c).get("hint", "") or "断线" in view(c).get("hint", ""),
            f"spectator room hint missing disconnect: {view(c)}")

    sb = lobby_and_join(b)
    require(sb.get("role") == "1" and sb.get("phase") == "0",
            f"guest did not reclaim WAIT seat: {sb}")
    require(a.wait_for_since("join reclaim", amark, 5),
            "host did not log join reclaim; guest may have joined fresh")
    wait_state(c, lambda s: s.get("seatb") == "1" and s.get("secb") == "0",
               6, "spectator sees blue online")
    require(state(c).get("players") == "2", "spectator lost player count after rejoin")


def test_guest_reboot_playing_timeout(a: Board, b: Board, c: Board) -> None:
    setup_room(a, b, c)
    start_game(a, b, c)
    reboot_board(b)
    wait_state(a, lambda s: s.get("phase") == "2" and s.get("peer") == "0" and
               int(s.get("reconn", "0")) > 0, 8, "host PLAYING window")
    sc = wait_state(c, lambda s: s.get("seatb") == "0" and
                    int(s.get("secb", "0")) > 0, 8, "spectator PLAYING window")
    require(sc.get("off") == "1", f"spectator missed PLAYING disconnect: {sc}")
    va = wait_view(c, lambda v: v.get("reconn") == "1", 5, "C reconnect banner")
    require("蓝方" in va.get("reconn_text", "") or "断线" in va.get("reconn_text", ""),
            f"spectator banner wrong: {va}")

    # 不回来：10s 后房主判客方负，旁观者也必须看到权威结果并回准备页。
    wait_state(a, lambda s: s.get("phase") == "3", 20, "host timeout result")
    wait_state(c, lambda s: s.get("phase") == "3", 20, "spectator timeout result")
    require(view(c).get("result", "") != "", "spectator timeout result text missing")
    wait_state(a, lambda s: s.get("phase") == "0" and s.get("players") == "1",
               8, "host back room after timeout")
    wait_state(c, lambda s: s.get("phase") == "0" and s.get("players") == "1",
               8, "spectator back room after timeout")
    require(view(c).get("playing") == "0", "spectator stayed on game page")
    require(state(c).get("spectators") == "1", "spectator lost after timeout")


def test_spectator_drop_and_midgame_join(a: Board, b: Board, c: Board) -> None:
    setup_room(a, b, c)
    start_game(a, b, c)
    mark = c.mark()
    c.send("gy_net drop")
    require(c.wait_for_since("test drop conn=", mark, 3),
            "spectator drop command did not disconnect the link")
    wait_state(c, lambda s: s.get("peer") == "1" and s.get("phase") == "2",
               10, "spectator transient rejoin")
    wait_state(a, lambda s: s.get("spectators") == "1", 5, "host sees spectator back")
    require(view(c).get("playing") == "1", "spectator lost game page after drop")

    c.send("gy_net leave")
    wait_state(c, lambda s: s.get("active") == "0", 5, "spectator leave")
    wait_state(a, lambda s: s.get("spectators") == "0", 5, "host sees spectator leave")
    c.send("gy_net lobby")
    wait_state(c, lambda s: int(s.get("rooms", "0")) >= 1, 6, "C rediscover playing room")
    c.send("gy_net watch 0")
    wait_state(c, lambda s: s.get("active") == "2" and s.get("role") == "2" and
               s.get("phase") == "2", 10, "C mid-game spectator join")
    wait_state(a, lambda s: s.get("spectators") == "1", 5, "host sees mid-game spectator")
    require(view(c).get("playing") == "1", "mid-game spectator not on game page")
    sa = state(a)
    sc = state(c)
    require(sa.get("seq") == sc.get("seq") and sa.get("rev") == sc.get("rev"),
            f"mid-game snapshot out of sync: A={sa} C={sc}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port-a", default="COM7")
    ap.add_argument("--port-b", default="COM8")
    ap.add_argument("--port-c", default="COM5")
    ap.add_argument("--boot-wait", type=float, default=8.0)
    args = ap.parse_args()

    OUTDIR.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    a = Board("A", args.port_a)
    b = Board("B", args.port_b)
    c = Board("C", args.port_c)
    ok = False
    try:
        time.sleep(args.boot_wait)
        boot(a)
        boot(b)
        boot(c)
        print("== room + spectator identity ==")
        setup_room(a, b, c)
        print("== spectator read-only ==")
        test_readonly(a, b, c)
        print("== ready -> game -> preview sync ==")
        start_game(a, b, c)
        test_preview_and_sync(a, b, c)
        print("== result -> room -> second game ==")
        test_result_cycle(a, b, c)
        print("== guest reboot in WAIT ==")
        test_guest_reboot_wait(a, b, c)
        print("== guest reboot in PLAYING + timeout ==")
        test_guest_reboot_playing_timeout(a, b, c)
        print("== spectator drop / mid-game join ==")
        test_spectator_drop_and_midgame_join(a, b, c)
        ok = True
        print("PASS: three-board spectator suite")
        print("A final:", a.state())
        print("B final:", b.state())
        print("C final:", c.state())
    finally:
        try:
            for board in (a, b, c):
                board.send("gy_net lobby")
        except Exception:
            pass
        (OUTDIR / f"spectator_A_{args.port_a}_{stamp}.log").write_text(
            a.text(), encoding="utf-8")
        (OUTDIR / f"spectator_B_{args.port_b}_{stamp}.log").write_text(
            b.text(), encoding="utf-8")
        (OUTDIR / f"spectator_C_{args.port_c}_{stamp}.log").write_text(
            c.text(), encoding="utf-8")
        a.close()
        b.close()
        c.close()
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
