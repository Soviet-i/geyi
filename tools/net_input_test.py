#!/usr/bin/env python3
"""Two-board online *input* repro/regression suite.

Why this exists
---------------
The existing net suites drive `gy_net move`, which queues a move directly and
skips the physical input path.  The COM5 bug ("preview appears, KEY2 does
nothing, COM7 always works") only shows up when a real tilt + KEY2 goes
through `gy_board_poll()` and `gy_ui_key2()`.

This script therefore uses the firmware's test injection:
  gy_net simtilt X Y   replace only the IMU sample source
  gy_net simkey        pulse KEY2 through the same debounce logic
Everything after the sample source (freshness, stale cancel, KEY2 pressed(),
tilt state machine, gy_ui_tilt_select(), GY_UI_CONFIRM routing) is unchanged.

Flow: flash both boards, host/join, ready, start, then for every turn put the
board of the current side through a real tilt + KEY2 and require the
authoritative host revision/turn to advance and the guest to follow.

Usage:
  python tools/net_input_test.py --port-a COM7 --port-b COM5
  python tools/net_input_test.py --port-a COM7 --port-b COM5 --no-flash
  python tools/net_input_test.py --port-a COM7 --port-b COM5 --host COM5
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from net_board_test import Board, wait_state  # noqa: E402

BUILD = ROOT / "project" / "build_sf32lb52-lchspi-ulp_hcpu"
VECTORS = {0: (0, -1000), 1: (1000, 0), 2: (0, 1000), 3: (-1000, 0)}
DIRNAME = {0: "UP", 1: "RIGHT", 2: "DOWN", 3: "LEFT"}
OUTDIR = ROOT / "evidence" / "net" / time.strftime("%Y%m%d") / "input_repro"


class Failure(RuntimeError):
    pass


def require(cond: bool, message: str) -> None:
    if not cond:
        raise Failure(message)


def find_sftool() -> Path:
    candidates = [os.environ.get("SFTOOL"), shutil.which("sftool")]
    candidates += sorted(
        (str(p) for p in (Path.home() / ".sifli/tools/sftool").glob("*/sftool.exe")),
        reverse=True,
    )
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    raise Failure("sftool missing; set SFTOOL or put sftool on PATH")


def info(board: Board, timeout: float = 3.0) -> dict[str, str]:
    state = board.state(timeout)
    require(state is not None, f"{board.name} state missing")
    return state


def value(state: dict[str, str], key: str, default: str = "0") -> int:
    try:
        return int(state.get(key, default))
    except (TypeError, ValueError):
        raise Failure(f"bad {key}={state.get(key)!r} in {state}")


def dump_tail(board: Board, count: int = 60) -> str:
    lines = board.text().splitlines()
    return "\n".join(lines[-count:])


def boot(board: Board) -> None:
    board.send("")
    require(board.wait_for("msh />", 20), f"{board.name} shell missing")
    require(board.wait_for("[geyi:lcd] READY", 20), f"{board.name} UI not ready")
    time.sleep(0.8)


def reset_all(a: Board, b: Board) -> None:
    for board in (a, b):
        board.send("gy_net lobby")
    for board in (a, b):
        wait_state(board, lambda s: s.get("active") == "0", 8, "reset lobby")
    time.sleep(1.0)


def create_and_join(host: Board, guest: Board) -> None:
    reset_all(host, guest)
    host.send("gy_net host")
    wait_state(host, lambda s: s.get("active") == "1" and s.get("players") == "1",
               8, "host room")
    guest.send("gy_net lobby")
    wait_state(guest, lambda s: int(s.get("rooms", "0")) >= 1, 10, "discover room")
    guest.send("gy_net join 0")
    wait_state(guest, lambda s: s.get("active") == "2" and s.get("role") == "1",
               12, "join as player")
    wait_state(host, lambda s: s.get("players") == "2", 8, "host sees guest")


def swap_seats(host: Board, guest: Board) -> None:
    print("swapping seats before start")
    host.send("gy_net swap")
    wait_state(host, lambda s: s.get("host") == "1" and s.get("local") == "1",
               8, "host swapped to blue")
    wait_state(guest, lambda s: s.get("local") == "0" and s.get("guest") == "0",
               8, "guest swapped to red")
    time.sleep(0.8)


def start_game(host: Board, guest: Board) -> None:
    host.send("gy_net ready")
    guest.send("gy_net ready")
    wait_state(host, lambda s: s.get("phase") == "2", 12, "host playing")
    wait_state(guest, lambda s: s.get("phase") == "2", 12, "guest playing")
    require(info(host).get("rev") is not None, "host no board")
    time.sleep(0.6)
    # 测试夹具：给双方各 4 金币，保证第一回合就能走真实放兵输入路径；
    # 金币由房主写入并广播快照，客方仍通过正常快照同步。
    host.send("gy_net simcoin 4")
    end = time.time() + 6
    last = None
    while time.time() < end:
        last = (info(host), info(guest))
        if (value(last[0], "coinr") >= 4 and value(last[0], "coinb") >= 4 and
                value(last[1], "coinr") >= 4 and value(last[1], "coinb") >= 4):
            return
        time.sleep(0.15)
    raise Failure(f"simcoin did not reach both boards: {last}")


def wait_room_phase(host: Board, guest: Board, timeout: float = 12.0) -> None:
    end = time.time() + timeout
    last = None
    while time.time() < end:
        hs = info(host)
        gs = info(guest)
        last = (hs.get("phase"), gs.get("phase"))
        if value(hs, "phase") in (0, 1) and value(gs, "phase") in (0, 1):
            return
        time.sleep(0.3)
    raise Failure(f"room phase after game not reached: {last}")


def choose_direction(actor: Board, timeout: float = 5.0) -> int:
    """Put the actor through the same tilt sequence a user would use.

    Tries each direction until the UI reports a preview.  Returns the
    direction.  Raises with the actor's own state when no direction previews,
    which is exactly the "COM5 preview disappears / cannot confirm" symptom.
    """
    last = None
    for direction in (0, 1, 2, 3):
        x, y = VECTORS[direction]
        actor.send("gy_net simtilt 0 0")
        time.sleep(0.25)
        actor.send(f"gy_net simtilt {x} {y}")
        end = time.time() + timeout
        while time.time() < end:
            st = info(actor)
            last = st
            if value(st, "ready"):
                require(value(st, "dir") == direction,
                        f"{actor.name} preview dir {st.get('dir')} != {direction}")
                require(value(st, "human") == 1,
                        f"{actor.name} preview without human turn: {st}")
                print(f"  {actor.name} tilt {DIRNAME[direction]} -> preview "
                      f"dir={st.get('dir')} count={st.get('count')}")
                return direction
            if value(st, "human") != 1:
                break
            time.sleep(0.1)
    raise Failure(f"{actor.name} could not create a tilt preview; last={last}\n"
                  f"--- {actor.name} tail ---\n{dump_tail(actor)}")


def confirm_key(actor: Board) -> None:
    actor.send("gy_net simkey")
    time.sleep(0.12)
    actor.send("gy_net simtilt 0 0")   # user relaxes the board after pressing
    time.sleep(0.45)


def wait_advance(host: Board, guest: Board, rev: int, turn: int,
                 timeout: float = 5.0) -> tuple[dict[str, str], dict[str, str]]:
    end = time.time() + timeout
    last_h: dict[str, str] = {}
    last_g: dict[str, str] = {}
    while time.time() < end:
        last_h = info(host)
        last_g = info(guest)
        finished = value(last_h, "outcome") != 0 or value(last_h, "phase") != 2
        if value(last_h, "rev") > rev and (value(last_h, "turn") != turn or finished):
            if value(last_g, "rev") >= value(last_h, "rev") and \
               (value(last_g, "turn") == value(last_h, "turn") or finished):
                return last_h, last_g
        time.sleep(0.15)
    raise Failure(
        "move did not advance: host_rev=%s turn=%s guest_rev=%s guest_turn=%s\n"
        "--- host tail ---\n%s\n--- guest tail ---\n%s"
        % (last_h.get("rev"), last_h.get("turn"), last_g.get("rev"),
           last_g.get("turn"), dump_tail(host), dump_tail(guest)))


def try_place(actor: Board, local: int) -> bool:
    """If the actor can afford a soldier, exercise the real placement input.

    KEY2 with no preview enters placement mode; a tilt selects an outer-ring
    cell; a second KEY2 confirms.  Returns True if a placement was confirmed.
    """
    st = info(actor)
    coins = value(st, f"coin{'r' if local == 0 else 'b'}")
    cost = value(st, "cost", "4")
    if coins < cost or value(st, "human") != 1:
        return False
    actor.send("gy_net simtilt 0 0")
    time.sleep(0.35)
    st = info(actor)
    if value(st, "ready") or value(st, "placing"):
        return False
    actor.send("gy_net simkey")
    time.sleep(0.35)
    st = info(actor)
    if not value(st, "placing"):
        print(f"  {actor.name} placement not entered (coins={coins})")
        return False
    # 四个方向依次尝试，找到能落在合法外围格的倾斜。
    selected = None
    for direction in (2, 0, 1, 3):
        x, y = VECTORS[direction]
        actor.send(f"gy_net simtilt {x} {y}")
        end = time.time() + 2.0
        while time.time() < end:
            st = info(actor)
            if value(st, "placing") and value(st, "ready"):
                selected = (value(st, "prow"), value(st, "pcol"))
                break
            if not value(st, "placing"):
                break
            time.sleep(0.1)
        if selected:
            break
    if not selected:
        actor.send("gy_net simtilt 0 0")
        time.sleep(0.3)
        actor.send("gy_net simkey")   # placing + no preview => cancel
        time.sleep(0.3)
        print(f"  {actor.name} no legal placement cell")
        return False
    # 停留超过 1s，确保跨过房主的周期快照；客方若把同 revision 的
    # 重同步快照当成新棋盘，会清掉 net-app 的 placing，确认就会错发移动。
    print(f"  {actor.name} place row={selected[0]} col={selected[1]}, "
          "holding 1.6s across periodic snapshot")
    time.sleep(1.6)
    actor.send("gy_net simkey")
    time.sleep(0.12)
    actor.send("gy_net simtilt 0 0")
    time.sleep(0.45)
    return True


def run_game(host: Board, guest: Board, max_moves: int,
             moves: dict[str, int], places: dict[str, int]) -> None:
    for index in range(max_moves):
        hs = info(host)
        gs = info(guest)
        if value(hs, "phase") != 2 or value(hs, "outcome") != 0:
            print(f"game finished after {index} input moves; outcome={hs.get('outcome')}")
            return
        turn = value(hs, "turn")
        actor = host if value(hs, "local") == turn else guest
        side = "host" if actor is host else "guest"
        print(f"[move {index + 1}] turn={turn} actor={actor.name} ({side}) "
              f"local={hs.get('local')}/{gs.get('local')} rev={hs.get('rev')}")
        require(value(hs, "phase") == 2, f"host phase not playing: {hs}")
        require(value(gs, "phase") == 2, f"guest phase not playing: {gs}")
        require(value(gs, "local") == turn or value(hs, "local") == turn,
                f"turn {turn} owned by neither board: host={hs} guest={gs}")

        before_rev = value(hs, "rev")
        before_turn = turn
        local = value(hs, "local") if actor is host else value(gs, "local")
        if try_place(actor, local):
            wait_advance(host, guest, before_rev, before_turn)
            places[actor.port] += 1
            print(f"  OK place rev {before_rev} -> {value(info(host), 'rev')}")
            continue

        choose_direction(actor)

        # 屏幕按钮路径也要覆盖：先取消倾斜，确认仍可用。
        confirm_key(actor)
        wait_advance(host, guest, before_rev, before_turn)
        moves[actor.port] += 1
        print(f"  OK rev {before_rev} -> {value(info(host), 'rev')}")


def flash_board(port: str, folder: Path) -> None:
    tool = find_sftool()
    require((BUILD / "sftool_param.json").exists(),
            f"missing build config {BUILD / 'sftool_param.json'}")
    print(f"flashing {port} ...", flush=True)
    log = folder / f"flash_{port}.log"
    with log.open("wb") as fh:
        proc = subprocess.run(
            [str(tool), "-p", port, "-b", "1000000", "config", "sftool_param.json"],
            cwd=BUILD, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        fh.write(proc.stdout)
    if proc.returncode != 0:
        raise Failure(f"flash {port} failed rc={proc.returncode}; see {log}")
    print(f"  {port} flash rc=0", flush=True)


def write_firmware(folder: Path, ports: list[str]) -> None:
    binary = BUILD / "main.bin"
    sources = list((ROOT / "src").glob("*.c")) + list((ROOT / "src").glob("*.h"))
    meta = {
        "board": "sf32lb52-lchspi-ulp",
        "ports": ports,
        "main_bin_size": binary.stat().st_size if binary.exists() else None,
        "main_bin_sha256": hashlib.sha256(binary.read_bytes()).hexdigest()
        if binary.exists() else None,
        "source_files": {
            str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sources
        },
        "worktree_dirty": bool(subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=ROOT, text=True)),
        "head": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
    }
    (folder / "firmware.json").write_text(
        json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port-a", default="COM7")
    ap.add_argument("--port-b", default="COM5")
    ap.add_argument("--host", default=None,
                    help="which port creates the room (default: port-a)")
    ap.add_argument("--moves", type=int, default=8)
    ap.add_argument("--swap", action="store_true",
                    help="use the in-room seat swap before starting")
    ap.add_argument("--no-flash", action="store_true")
    args = ap.parse_args()

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    folder = OUTDIR.parent / f"input_{stamp}"
    folder.mkdir(parents=True, exist_ok=False)
    write_firmware(folder, [args.port_a, args.port_b])

    if not args.no_flash:
        for port in (args.port_a, args.port_b):
            flash_board(port, folder)
        time.sleep(1.0)

    a = Board("A", args.port_a)
    b = Board("B", args.port_b)
    try:
        boot(a)
        boot(b)
        host, guest = (a, b) if args.host in (None, args.port_a) else (b, a)
        print(f"host={host.port} guest={guest.port}")
        create_and_join(host, guest)
        if args.swap:
            swap_seats(host, guest)
        start_game(host, guest)

        # 确保两块板从真实/中性状态开始，且注入模式已开启。
        for board in (a, b):
            board.send("gy_net simtilt 0 0")
        time.sleep(0.5)

        moves = {a.port: 0, b.port: 0}
        places = {a.port: 0, b.port: 0}
        for round_index in range(3):
            run_game(host, guest, args.moves, moves, places)
            if (moves[a.port] > 0 and moves[b.port] > 0 and
                    places[a.port] > 0 and places[b.port] > 0):
                break
            if round_index == 2:
                break
            print(f"not all paths exercised yet (moves={moves} places={places}); "
                  "starting another game")
            wait_room_phase(host, guest)
            start_game(host, guest)
        require(moves[a.port] > 0 and moves[b.port] > 0,
                f"both seats must send at least one MOVE_REQ; moves={moves}")
        require(places[a.port] > 0 and places[b.port] > 0,
                f"both seats must confirm at least one placement; places={places}")
        print(f"PASS: tilt+KEY2 move and placement accepted on both boards; "
              f"moves={moves} places={places}")
        return 0
    except Failure as exc:
        print("FAIL:", exc)
        return 1
    finally:
        # 失败时也要撤掉注入，避免板子停在最后一次模拟倾斜上。
        for board in (a, b):
            try:
                board.send("gy_net simtilt 0 0")
                board.send("gy_net simoff")
            except Exception:
                pass
        time.sleep(0.4)
        (folder / "serial_A.log").write_text(a.text(), encoding="utf-8")
        (folder / "serial_B.log").write_text(b.text(), encoding="utf-8")
        a.close()
        b.close()
        print("evidence:", folder)


if __name__ == "__main__":
    raise SystemExit(main())
