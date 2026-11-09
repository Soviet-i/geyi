#!/usr/bin/env python3
"""Optional third BLE central on the PC (needs Windows BLE + bleak).

The two-board suite cannot test "a second peer joins while the first guest is
already connected" or "a client connects but never sends JOIN".  This script
acts as a third device: board A is host, board B is the first guest, and the PC
connects over BLE.

Scenarios:
  third-peer : PC joins as auto (spectator when the room is full), A must show
               players=2/spectators=1 and keep adv=1; PC disconnect -> A
               spectators back to 0.
  idle-link  : PC connects but never sends JOIN; A must drop the idle link
               after ~5 s and keep advertising.
  burst      : PC connects and disconnects immediately; A must stay responsive
               and keep its room.

Usage:
  python tools/pc_ble_peer.py --port-a COM7 --port-b COM8 --scenario third-peer
"""
from __future__ import annotations

import argparse
import asyncio
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from net_board_test import Board, wait_state  # noqa: E402

RX = "47594e45-542d-5258-2d43484152303031"
TX = "47594e45-542d-5458-2d43484152303031"
VERSION = 2
JOIN_REQ, LEAVE = 2, 7
REASON_LEFT = 3

try:
    from bleak import BleakClient, BleakScanner
except ImportError:  # pragma: no cover - optional dependency
    BleakClient = BleakScanner = None


class Central:
    def __init__(self) -> None:
        self.client: BleakClient | None = None
        self.room_id = 0
        self.buf = bytearray()
        self.total = 0
        self.acks: list[tuple[int, bytes]] = []

    async def find(self):
        found = await BleakScanner.discover(timeout=10, return_adv=True)
        for dev, adv in found.values():
            name = dev.name or adv.local_name or ""
            if name.startswith("GEYI-"):
                self.room_id = int(name[5:], 16)
                print(f"found {name} {dev.address} room=0x{self.room_id:08x}")
                return dev
        raise SystemExit("FAIL: no GEYI-* advertiser found")

    def _notify(self, _sender, data: bytearray) -> None:
        data = bytes(data)
        if len(data) < 4:
            return
        total = struct.unpack_from("<H", data, 0)[0]
        off = struct.unpack_from("<H", data, 2)[0]
        if off == 0:
            self.buf = bytearray()
            self.total = total
        self.buf += data[4:]
        if self.total and len(self.buf) >= self.total:
            msg = bytes(self.buf[:self.total])
            self.buf = bytearray()
            if len(msg) >= 12 and msg[0:2] == b"GY":
                self.acks.append((msg[3], msg[12:12 + struct.unpack_from("<H", msg, 10)[0]]))

    async def connect(self, dev) -> None:
        self.client = BleakClient(dev)
        await self.client.connect()
        await self.client.start_notify(TX, self._notify)
        print("connected, mtu", self.client.mtu_size)

    async def send(self, typ: int, payload: bytes, seq: int = 1) -> None:
        msg = b"GY" + bytes([VERSION, typ, 0, 0]) + struct.pack("<I", seq) + \
              struct.pack("<H", len(payload)) + payload
        await self.client.write_gatt_char(
            RX, struct.pack("<HH", len(msg), 0) + msg, response=False)

    async def join(self, role: int = 2, player_id: int = 0xBEEF) -> None:
        await self.send(JOIN_REQ, bytes([role]) + struct.pack("<H", player_id) +
                        struct.pack("<I", self.room_id))
        await asyncio.sleep(0.8)
        if not any(t == 3 for t, _ in self.acks):
            raise SystemExit("FAIL: no JOIN_ACK at the PC")
        print("PC JOIN_ACK:", [(t, p.hex()) for t, p in self.acks if t == 3])

    async def leave(self) -> None:
        await self.send(LEAVE, bytes([REASON_LEFT]))
        await asyncio.sleep(0.3)

    async def close(self) -> None:
        if self.client and self.client.is_connected:
            await self.client.disconnect()


def select(state: dict, keys: tuple[str, ...]) -> dict:
    return {k: state.get(k) for k in keys}


async def scenario(name: str, a: Board, b: Board) -> None:
    c = Central()
    dev = await c.find()
    await c.connect(dev)

    if name == "third-peer":
        await c.join(role=2)
        sa = wait_state(a, lambda s: s.get("players") == "2" and s.get("spectators") == "1",
                        4, "host sees PC spectator")
        require(sa.get("adv") == "1", f"host stopped advertising with 2 links: {sa}")
        print("A with board B + PC:", select(sa, ("players", "spectators", "adv")))
        await c.leave()
        await c.close()
        sa = wait_state(a, lambda s: s.get("spectators") == "0", 6, "PC leave released")
        print("A after PC leave:", select(sa, ("players", "spectators", "adv")))

    elif name == "idle-link":
        print("PC connected without JOIN; waiting for host 5s reap...")
        await asyncio.sleep(7.0)
        sa = a.state(timeout=3)
        require(sa is not None and sa.get("active") == "1" and
                sa.get("players") == "2" and sa.get("adv") == "1",
                f"host not healthy after idle link: {sa}")
        require(not c.client.is_connected or "drop idle link" in a.text(),
                "host did not drop the idle link")
        print("idle link dropped; A:", select(sa, ("players", "spectators", "adv")))
        await c.close()

    elif name == "burst":
        await c.close()
        sa = a.state(timeout=3)
        require(sa is not None and sa.get("active") == "1" and sa.get("adv") == "1",
                f"host wedged after burst connect/disconnect: {sa}")
        print("A after burst:", select(sa, ("players", "spectators", "adv")))

    else:
        raise SystemExit(f"unknown scenario {name}")


def require(cond: bool, message: str) -> None:
    if not cond:
        raise SystemExit("FAIL: " + message)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port-a", default="COM7")
    ap.add_argument("--port-b", default="COM8")
    ap.add_argument("--scenario", default="third-peer",
                    choices=("third-peer", "idle-link", "burst"))
    ap.add_argument("--boot-wait", type=float, default=8.0)
    args = ap.parse_args()
    if BleakScanner is None:
        raise SystemExit("bleak is required: pip install bleak")

    a = Board("A", args.port_a)
    b = Board("B", args.port_b)
    try:
        time.sleep(args.boot_wait)
        require(a.wait_for("Keep board still", 15), "A UI not ready")
        require(b.wait_for("Keep board still", 15), "B UI not ready")
        time.sleep(1.0)
        a.send("gy_net host")
        wait_state(a, lambda s: s.get("active") == "1" and s.get("adv") == "1", 6, "host")
        b.send("gy_net lobby")
        wait_state(b, lambda s: int(s.get("rooms", "0")) >= 1, 8, "B discover")
        b.send("gy_net join 0")
        wait_state(b, lambda s: s.get("active") == "2" and s.get("local") != "255",
                   10, "B join")
        wait_state(a, lambda s: s.get("players") == "2", 4, "A sees B")
        print("A host + B guest; adv=", a.state(timeout=3).get("adv"))
        asyncio.run(scenario(args.scenario, a, b))
        b.send("gy_net leave")
        a.send("gy_net leave")
        time.sleep(1.0)
        print("PASS:", args.scenario)
        return 0
    finally:
        a.close()
        b.close()


if __name__ == "__main__":
    raise SystemExit(main())
