"""串口记录工具：带毫秒时间戳把板端日志写入 evidence/param/<日期>/serial.log。

用途：板端交互验证（如 REQ-407 参数生效）时长时间记录，Ctrl+C 结束。
不修改板子、不发任何命令（只读），不替代屏幕观察与录像。

用法：
    python tools/capture_serial.py                 # 默认 COM3，自动识别 CH340
    python tools/capture_serial.py --port COM7
    python tools/capture_serial.py --out evidence/param/20261107/serial.log
"""
from pathlib import Path
import argparse
import datetime
import sys
import time

try:
    import serial
    import serial.tools.list_ports as list_ports
except ImportError:
    raise SystemExit("缺少 pyserial：先运行 python -m pip install pyserial")

root = Path(__file__).resolve().parents[1]
BAUD = 1000000


def pick_port(explicit):
    if explicit:
        return explicit
    hits = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x1A86, 0x7523)]
    if len(hits) == 1:
        return hits[0]
    if not hits:
        raise SystemExit("没有找到 CH340 串口。确认板子已插在 USB-to-UART 口，或用 --port 指定。")
    raise SystemExit(f"找到多个 CH340 串口 {hits}，请用 --port 指定一个。")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None)
    ap.add_argument("--out", default=None)
    ap.add_argument("--title", default="板端串口记录")
    args = ap.parse_args()

    port = pick_port(args.port)
    out = root / (args.out or f"evidence/param/{datetime.date.today():%Y%m%d}/serial.log")
    out.parent.mkdir(parents=True, exist_ok=True)

    dev = serial.Serial()
    dev.port = port
    dev.baudrate = BAUD
    dev.timeout = 0.2
    # 显式拉低 DTR/RTS：本板复位线是 RTS，不要让打开串口时误触发复位
    dev.dtr = False
    dev.rts = False
    dev.open()

    started = datetime.datetime.now()
    t0 = time.monotonic()
    print(f"记录中：{port} @ {BAUD} 8N1 -> {out}")
    print("现在可以开始操作板子；按 Ctrl+C 结束记录。")

    lines = 0
    with out.open("w", encoding="utf-8", newline="\n") as f:
        f.write(f"# {args.title}\n")
        f.write(f"# 端口 {port} @ {BAUD} 8N1（只读，不发送任何命令）\n")
        f.write(f"# 开始 {started.isoformat(timespec='seconds')}\n")
        # 串口一次 read 可能只给出半行，必须做行缓冲：只写完整行，退出时补最后半行。
        # （2026-11-07 首次实测发现：按 chunk 分行会把 "[geyi:motion] x=..." 切成 4 段。）
        buf = ""
        try:
            while True:
                data = dev.read(dev.in_waiting or 1)
                if not data:
                    continue
                text = data.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
                ms = int((time.monotonic() - t0) * 1000)
                buf += text
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.rstrip("\r")
                    if line.strip():
                        f.write(f"[{ms:9d}ms] {line}\n")
                        f.flush()
                        lines += 1
        except KeyboardInterrupt:
            print("\n已停止。")
        finally:
            if buf.strip():
                f.write(f"[{'end':>9}] {buf.rstrip()}\n")
                lines += 1
            dev.close()
            f.write(f"# 结束 {datetime.datetime.now().isoformat(timespec='seconds')}，共 {lines} 行\n")

    print(f"已保存 {out}（{lines} 行）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
