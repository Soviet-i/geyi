# M2 关键验证证据索引（2026-10-28 整理）

本文件只做索引，不移动原始证据；`manifest.yaml` 和 `docs/04-测试报告.md` 仍引用原路径。

| 事项 | 证据 | 覆盖 | 不覆盖 / 限制 |
|---|---|---|---|
| 单机板端 | `evidence/board/20261015_2106/README.md`、`evidence/board/20261015_213937/serial.log` | 启动、简体显示、四向预览、灯事件；学生确认完整对局/放兵 | 倾斜正确率/误触/延迟、帧率、完整对局日志未量化 |
| 人机与准备页 | `evidence/ai/BaiduNetDisk.md`（学生网盘视频）、`evidence/ai/desktop-test-tc3*.log`、`evidence/ai/firmware_build_tc3*.log` | 10-17 学生视频：选择先后手+人机对战；桌面 TC-315~317；839,584 B 构建 | agent 未解码视频；REQ-316 的 500ms/单次提交/取消未逐帧核验；串口 `ai=PASS`、耗时/栈未测 |
| 移动顺序 | `evidence/move-order/README.md`、`hardware-01/02/03.jpg` | TC-304-05 两兵移动/数字补位/D1；839,552 B 构建 | TC-303-05 单兵仅主机；10-17 视频不覆盖本次修正；串口/计时未测 |
| 参数主机段 | `evidence/config/host_test_20261028.txt` | TC-401~406/TC-409 共 2517 断言 PASS | 参数界面 REQ-408、板端生效 REQ-407/410、平衡扫描 REQ-411 未执行/未实现 |

> 代码 HEAD `a3d1ad5`（本次文档校准不改固件）在 2026-10-28 参数集成后未重新编译/烧录；
> 上述板端证据对应 839,584 B（10-17 人机）和 839,552 B（10-18 移动顺序），不代表当前代码 HEAD。
