# 可烧录固件归档（格弈 / geyi）

`main.bin`、`bootloader.bin`、`ftab.bin` 随最终提交的 zip 一起交付，二进制本身
**不进入源码 Git 历史**（`.gitignore` 排除 `firmware/*.bin`）；本说明、`SHA256SUMS`
与烧录地址表入库。源码、完整 Git 历史与文档仍以仓库根目录的 `.git` / git bundle 为准。

## 文件

| 文件 | 大小 | SHA-256 |
|---|---|---|
| `main.bin` | 1,099,884 B | `4e51a876c6ed864402ab55c6b1701c99a4e1acf0b90d71be96b6f69a4542da02` |
| `bootloader.bin` | 58,496 B | `e59d70db44367d524fbb46a8a014ce0092462e86959d101a8e2e00ad27bb5e21` |
| `ftab.bin` | 11,288 B | `748178e77b979c5e6e20f5d6c37bf4c35885c17206362525e09e9343f7e1e4e3` |
| `sftool_param.json` | 382 B | 烧录地址表，见下 |

`SHA256SUMS` 为上述三个二进制文件的校验和，可在本目录执行：

```bash
sha256sum -c SHA256SUMS
```

## 构建信息

- 板型：`sf32lb52-lchspi-ulp`（黄山派 SF32LB525UC6）
- SDK：SiFli SDK v2.5.1，RT-Thread，LVGL v9
- 构建时间：2026-11-13 20:22（本地 `project/build_sf32lb52-lchspi-ulp_hcpu/`）
- 源码：R-081/R-082/R-083 修复后的 develop；构建时未把 commit SHA 写进产物，
  最终交付以仓库 `v1.0.0` tag 为准
- 主固件 1,099,884 B（1.049 MiB），低于 1.5 MiB 基线；LCPU 镜像约 6,384 B

## 烧录

`sftool_param.json` 的写地址：

| 文件 | 地址 |
|---|---|
| `bootloader.bin` | `0x12010000` |
| `main.bin` | `0x12020000` |
| `ftab.bin` | `0x12000000` |

二选一：

1. VS Code + SiFli-SDK-CodeKit：打开工程 `project/`，选择板型 `sf32lb52-lchspi-ulp`，
   状态栏“烧录”。
2. 命令行（已 export SDK）：在 `project/` 下按 SDK 的 `sftool` 用法烧录上述三个文件，
   或参考 `tools/flash_and_capture.py`。

烧录后串口应看到启动自检
`core/input/ai/param/paramui/net/light=PASS` 与 `[geyi:lcd] READY 390 x 450`。
串口证据示例见 `evidence/history/20261113/`、`evidence/net/20261113/spectator_follow/`。

## 重新构建

```bash
# 在已执行 SDK export 的终端
python scripts/build_firmware.py
# 产物：project/build_sf32lb52-lchspi-ulp_hcpu/main.bin
```

重新构建的二进制字节可能与归档版本不同（构建时间戳等），但源码功能一致；
交付时以本目录的 SHA-256 为准。
