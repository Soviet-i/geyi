# 2026-11-09 倾斜输入测量证据

## 文件

| 文件 | 内容 | 生成命令 |
|---|---|---|
| `input_bench.csv` | PR #17 候选方向缓冲的 `preview_available_bp` 原始数据（seed 20261106） | `build/host/geyi-input-bench --trials 50` |
| `input_bench_v2.csv` | 收尾联调重构后的同一工具输出（seed 20261106，含 select/switch/confirm 延迟、误触率、保持边界、预览可用占比） | `python tools/run_host.py --only geyi-input-bench` 后运行 `build/host/geyi-input-bench.exe --trials 50` |

## 边界

- 这是**主机仿真**：输入是 `geyi_input.c` 的真实状态机，采样周期 5/20ms 可配；
  不包含 IMU 的 I2C 抖动、校准偏差、线程调度延迟。
- `preview_available_bp` 统计“预览是否存在”（`direction >= 0`）的时间占比；
  0% 方向缓冲下方向仍可能变化，但新状态机用 `UPDATE` 保持预览，不再 CANCEL。
- 板端倾斜手感、放兵列、屏幕按钮冲突仍需学生真机验证并把日志/照片存入 `evidence/`。
