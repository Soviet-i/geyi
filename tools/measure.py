"""量化测量封装：倾斜输入延迟/误触（L-03）+ AI 搜索开销（L-10 的主机部分）。

规则、状态机与搜索全部在 C 核心中，本脚本不复制任何逻辑，只做调度与汇总。
先运行 `python tools/run_host.py` 生成 build/host/ 下的两个工具。

产物：
  <out-dir>/input_bench.csv   倾斜输入测量原始 CSV（同一 seed 两次运行必须字节一致）
  <out-dir>/ai_search.csv     AI 搜索按深度/是否剪枝聚合的统计
  <out-dir>/summary.md        便于粘进文档的摘要表

边界：全部为**主机仿真**。板端还会叠加 IMU 采样抖动、I2C 与调度延迟，
因此延迟数字只能作为状态机本身的延迟下限，不能写成真机延迟。
"""
from pathlib import Path
import argparse
import hashlib
import re
import subprocess
import statistics
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
host = root / "build" / "host"
suffix = ".exe" if sys.platform == "win32" else ""
input_bench = host / ("geyi-input-bench" + suffix)
ai_bench = host / ("geyi-ai-bench" + suffix)

AI_LINE = re.compile(
    r"pruning=(\d+) depth=(\d+) seed=(\d+) nodes=(\d+) cutoffs=(\d+) "
    r"elapsed_ms=(\d+) score=(-?\d+)"
)


def run(cmd, timeout=120):
    r = subprocess.run(cmd, cwd=root, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=timeout)
    if r.returncode != 0:
        raise SystemExit(f"命令失败（{r.returncode}）: {' '.join(map(str, cmd))}\n{r.stdout}\n{r.stderr}")
    return r.stdout


def measure_input(out_dir: Path, trials: int, seed: int) -> None:
    """跑两次同 seed，断言字节一致以证明可复现，然后落盘。"""
    with tempfile.TemporaryDirectory() as td:
        a, b = Path(td) / "a.csv", Path(td) / "b.csv"
        for path in (a, b):
            text = run([str(input_bench), "--trials", str(trials), "--seed", str(seed)])
            path.write_text(text, encoding="utf-8", newline="\n")
        if a.read_bytes() != b.read_bytes():
            raise SystemExit("FAIL: 倾斜输入测量两次运行结果不一致（不可复现）")
        target = out_dir / "input_bench.csv"
        target.write_bytes(a.read_bytes())
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    print(f"PASS: 倾斜输入测量可复现 {target} ({target.stat().st_size} bytes)")
    print(f"      sha256 {digest}")


def measure_ai(out_dir: Path, depths, seed_count: int) -> None:
    rows = {}
    for depth in depths:
        for pruning in (1, 0):
            nodes, cutoffs, elapsed = [], [], []
            for i in range(seed_count):
                seed = 20261106 + i
                out = run([str(ai_bench), "--depth", str(depth), "--seed", str(seed)]
                          + ([] if pruning else ["--no-pruning"]))
                m = AI_LINE.search(out)
                if not m:
                    raise SystemExit(f"无法解析 AI 输出: {out!r}")
                nodes.append(int(m.group(4)))
                cutoffs.append(int(m.group(5)))
                elapsed.append(int(m.group(6)))
            rows[(depth, pruning)] = (nodes, cutoffs, elapsed)

    lines = ["depth,pruning,runs,mean_nodes,max_nodes,mean_cutoffs,mean_elapsed_ms,max_elapsed_ms"]
    for (depth, pruning), (nodes, cutoffs, elapsed) in sorted(rows.items()):
        lines.append(",".join([
            str(depth), str(pruning), str(len(nodes)),
            f"{statistics.mean(nodes):.1f}", str(max(nodes)),
            f"{statistics.mean(cutoffs):.1f}",
            f"{statistics.mean(elapsed):.1f}", str(max(elapsed)),
        ]))
    target = out_dir / "ai_search.csv"
    target.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"PASS: AI 搜索统计 {target}（{len(rows)} 组 × {seed_count} 个种子）")

    # 剪枝收益直接对比同深度的节点数
    summary = ["| 深度 | 节点数（剪枝） | 节点数（不剪枝） | 剪枝减少 |", "|---|---|---|---|"]
    for depth in depths:
        p = rows[(depth, 1)][0]
        n = rows[(depth, 0)][0]
        mp, mn = statistics.mean(p), statistics.mean(n)
        cut = f"{(1 - mp / mn) * 100:.1f}%" if mn else "-"
        summary.append(f"| {depth} | {mp:.0f} | {mn:.0f} | {cut} |")
    (out_dir / "ai_pruning.md").write_text("\n".join(summary) + "\n", encoding="utf-8", newline="\n")
    print("\n".join(summary))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default="evidence/measure/20261106")
    ap.add_argument("--trials", type=int, default=50)
    ap.add_argument("--seed", type=int, default=20261106)
    ap.add_argument("--ai-seeds", type=int, default=5)
    ap.add_argument("--depths", default="1,2,3,4,5")
    args = ap.parse_args()

    for exe in (input_bench, ai_bench):
        if not exe.exists():
            print(f"missing {exe}; 先运行 python tools/run_host.py", file=sys.stderr)
            return 2

    out_dir = root / args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    measure_input(out_dir, args.trials, args.seed)
    depths = [int(x) for x in args.depths.split(",") if x.strip()]
    measure_ai(out_dir, depths, args.ai_seeds)
    print(f"\n全部产物在 {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
