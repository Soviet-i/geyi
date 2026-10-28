"""REQ-411 平衡扫描封装：调用 C 工具两次，验证 CSV 可复现。

规则与胜负判定全部在 C 核心中，本脚本不复制任何游戏规则。
先运行 `python tools/run_host.py` 生成 build/host/geyi-balance-scan.exe。
"""
from pathlib import Path
import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
exe = root / "build" / "host" / ("geyi-balance-scan.exe" if sys.platform == "win32" else "geyi-balance-scan")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--groups", type=int, default=20)
    ap.add_argument("--games", type=int, default=50)
    ap.add_argument("--out", default="evidence/balance/20261028/balance_scan.csv")
    args = ap.parse_args()
    if not exe.exists():
        print(f"missing {exe}; run python tools/run_host.py first", file=sys.stderr)
        return 2
    out = root / args.out
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        a = Path(td) / "a.csv"
        b = Path(td) / "b.csv"
        for path in (a, b):
            subprocess.run([str(exe), "--groups", str(args.groups),
                            "--games", str(args.games), "--out", str(path)],
                           check=True, cwd=root)
        da = a.read_bytes()
        db = b.read_bytes()
        if da != db:
            print("FAIL: two runs differ", file=sys.stderr)
            return 1
        out.write_bytes(da)
    digest = hashlib.sha256(out.read_bytes()).hexdigest()
    print(f"PASS: reproducible CSV {out} ({len(out.read_bytes())} bytes)")
    print("sha256:", digest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
