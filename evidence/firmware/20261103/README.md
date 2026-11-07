# 2026-11-03 当前 HEAD 固件编译证据

## 结论

**当前 develop（`af72682`）可以从源码编译通过**，产出 `main.bin` = **847,344 B**，
与 2026-10-28 已烧录并在板端连续 3 次启动自检 PASS 的固件**同尺寸**。

## 环境

| 项 | 值 |
|---|---|
| SDK | SiFli-SDK v2.5.1 |
| 交叉编译器 | arm-none-eabi-gcc 14.2.1 |
| 构建 | scons（SDK 环境自带）+ cmake 3.30.2 + ninja 1.13.2 |
| 板型 | sf32lb52-lchspi-ulp |

> 当天此前 SDK 的**环境实例失效**（`environment instance is missing or invalid for the current checkout`），
> 用 `install.ps1 --profile default` 重建后恢复。这一步本身不改变源码。

## 数据

| 项 | 值 |
|---|---|
| ROM 占用 | 879,238 B / 8,388,608 B |
| RAM 占用 | 247,380 B / 523,264 B |
| `main.bin` 大小 | 847,344 B |
| `sha256(main.bin)` | `29a5b7dc629efd46293cb5aa2efe8202a24c712078aa095b4475b9eb54ca269a` |
| 编译退出码 | 0 |

链接阶段的 `_read/_write is not implemented` 与 `cannot find entry symbol entry` 属于
nano.specs 与 SDK 启动脚本的既有提示，2026-10-15 起的构建日志中同样存在，不是本次引入。

## 本证据证明什么

- 当前 `develop` 的源码**能编译**，且能复现出与已验收版本相同尺寸的产物。
- 构建环境（SDK / 工具链 / scons）在本机可重建。

## 本证据**不**证明什么

- **未烧录、未上板、未做任何物理验证**。编译通过不等于板端功能正常。
- 不覆盖 REQ-407 的参数生效验证（仍需上板）。
- 不验证帧率、倾斜正确率、AI 耗时等量化指标。

按 `docs/01` 的状态口径：这是「主机/构建」证据，不能写成板端验收。
