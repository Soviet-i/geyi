# 对局页“认输/返回首页”按钮左移留证（2026-11-10）

## 改动

对局页右上按钮由 `x=270` 左移到 `x=240`（宽 96，右边缘 336/390，左移 30px）；
按钮文字仍按状态显示“认输 / 返回首页 / 退出旁观 / 退出”，移动的是同一个对象。
动机：按钮太靠右，真机右手/板边容易遮挡，点不到。

## 固件与烧录

- 端口：`COM5`（本轮只连接了这一块板；COM7/COM8 未烧录）。
- 固件：`main.bin` 1,085,716 B，SHA256
  `2d27e028a8141cc791722b64342718284e7fc3f9a682f45527883a676fe9ea7a`。
- 来源：`fix/final-integration` HEAD `a953ddb` + 未提交的 `src/geyi_ui.c` 坐标修改；
  `worktree_dirty=true`，逐文件哈希见 `firmware.json`。
- 烧录：`sftool -p COM5 -b 1000000 config sftool_param.json`，返回 0；见 `flash.log`。

## 启动结果

`core/input/ai/param/paramui/net/light` 全 PASS；`[geyi:lcd] READY 390 x 450`；
`[geyi:imu] WHO_AM_I=6a`；`[geyi:ble] local id=B7B0`；见 `serial_boot.log`。

## 未验证

尚未目测按钮在真实屏幕上的位置和可点击性，也没有录制触摸操作。
本目录只能证明新坐标已编译、烧录、启动；**不能写成“已确认点得到”**。
