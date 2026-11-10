# 三块板旁观固件（协议 v3，2026-11-10）

## 固件

- `main.bin` **1,087,220 B** + LCPU **6,384 B**。
- SHA256 `063372ce63bd772ceaf9da6def7fe707300d61442a824637d5fd6bc6425d2f3c`。
- 来源：`fix/final-integration` HEAD `a953ddb` + 未提交的联机/旁观修改；
  `worktree_dirty=true`，逐文件哈希见 `firmware.json`。
- 协议 v3：`ROOM_STATE` 增加 `seat_online`（bit0 红/bit1 蓝）和
  `seat_reconnect_s[2]`；v3 与 v2 不兼容，三块板必须同版本。

## 烧录与启动

- 端口：COM7 / COM8 / COM5，均 `sftool -p <port> -b 1000000 config sftool_param.json`
  返回 0；原始进度见 `flash_COM7.log`、`flash_COM8.log`、`flash_COM5.log`。
- 三块板启动均含 `net=PASS`、`[geyi:lcd] READY 390 x 450`、BLE local id；
  原始串口见 `serial_boot_COM*.log`。

## 三端旁观测试

命令：

```bash
python tools/net_spectator_test.py --port-a COM7 --port-b COM8 --port-c COM5
```

结果：**PASS**（2026-11-10 10:36 日志）。A=`F33A` 房主、B=`03EA` 房客、C=`B7B0` 旁观。
覆盖准备→对局→结算→第二局、旁观只读/双方预览、房客 WAIT 重启回收、房客 PLAYING
超时判红方胜、旁观者看到蓝方断线且倒计时递减、旁观瞬断 REJOIN、中途加入 seq/rev 一致。

测试日志：`evidence/net/20261110/spectator/spectator_{A,B,C}_*_20261110_103639.log`。

## 限制

真实触摸/像素未目测；只有一个旁观者，满旁观容量/双旁观者 P-02 未测；房主掉电
（RAM-only 房间消失）未在本轮覆盖。不能把自动套件写成完整联机/观战验收。
