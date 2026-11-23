# 第三块板烧录与启动留证（2026-11-09）

## 目的

学生新增第三块黄山派，准备做观战（REQ-501/503/507）的真机测试。本目录只记录
**烧录与启动**，不代表观战流程已经验收。

## 环境与固件来源

- 端口：`COM5`（新板；`COM7`/`COM8` 两块旧板本轮未烧录）。
- 固件：`main.bin` 1,085,708 B，SHA256
  `e2d8cb84ebae37dcdbed026d676d60f87eb3301c9bfcc0b5cb39ce56ce1e2651`。
- 编译来源：`fix/final-integration` HEAD `d99780e`，`worktree_dirty=true`；
  固件包含当时未提交的 `src/geyi_board.c`、`src/main.c` 修改，逐文件哈希见
  `firmware.json`。不能把该固件等同于 `d99780e` 本身。
- 烧录：`sftool 0.2.5 -p COM5 -b 1000000 config sftool_param.json`，返回 0；
  原始进度见 `flash.log`。

## 串口结果（`serial_boot.log`）

- 启动自检：`core/input/ai/param/paramui/net/light` 全部 PASS。
- `[geyi:lcd] READY 390 x 450`。
- `[geyi:imu] WHO_AM_I=6a`，新固件为“平放即重力零点、不做开机采样”。
- `[geyi:ble] local id=B7B0`。
- 启动后 `gy_net state`：`active=0 role=2 id=B7B0`，处于大厅/未建房状态。

## 边界与说明

- 未执行观战、未验证真实屏幕像素、触摸、倾斜手感。
- 第一次抓取脚本漏存串口字节，生成了 0 字节 `serial_boot.log`；已重启板子重抓并覆盖。
  第一次终端输出里曾出现 `join accepted color=255`（旁观位），但未落盘，不作为正式证据。
- `COM7`/`COM8` 仍运行此前固件；`net_app/net_ble/net_room/net_proto` 源码哈希与本固件一致，
  协议应兼容，但未重烧。若要做严格三板同版本观战，建议三块板烧同一 `main.bin`。
