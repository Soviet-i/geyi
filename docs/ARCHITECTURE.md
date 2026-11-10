# 架构与代码索引（ARCHITECTURE）

> 面向 Agent 的 5 分钟入口。只写稳定事实；需求/验收细节见 `docs/00`–`06`（`AGENTS.md` 已列）与 `manifest.yaml`。
> 当前实现：单机同板双人、人机 AI、参数/参数页、BLE 联机（含旁观、断线重连）；历史/计时仅设计基线。
> 真机像素、触摸、倾斜手感、功耗/长稳仍以 `evidence/` 为准，自动化通过不等于真机验收。
## 1. 总览

格弈是黄山派 SF32LB52（`sf32lb52-lchspi-ulp`，RT-Thread，LVGL v9）上的 6×6 数字合并战棋：倾斜选方向/数量，
KEY2 确认或放兵，触摸用于菜单；联机只走 BLE，房主权威、客方请求、旁观只读。`src/main.c` 是唯一入口，
`src/SConscript` 自动收集 `src/*.c`；`project/` 是 SDK 工程骨架，`proj.conf` 配 LVGL/BLE/I2C/RGB。

数据流：IMU/KEY2/触摸 → `gy_board`/`gy_input`/`gy_nav` → `gy_ui` →（单机）`gy_game` /（人机）`gy_ai_task` /
（联机）`gy_net_app` → `gy_net_room` → `gy_net_proto` → `gy_net_ble` → 对端。启动链：
`main()` → 自检 → `gy_net_app_init()` → `gy_ui_create()` → `gy_board_start()` → 5ms 循环
（`gy_board_poll()` + `lv_timer_handler()` + `gy_ui_tick()` + RGB 刷灯）。

关键不变量：
- `gy_game`、`gy_ai`、`gy_config`、`gy_net_proto/room/link` 不依赖 LVGL/RT-Thread/BLE，时间用 `now_ms` 注入，可主机测试。
- 联机是房主权威：只有房主改 `GyNetRoom`/棋局；客方发请求并应用 `ROOM_STATE`/`SNAPSHOT`；旁观只收状态/预览。
- 房间只存 RAM，房主重启即消失；断线保留席位 10s，窗口内按 `player_id` 回收，瞬断走 `REJOIN` token。
- BLE 链路用 `conn + gen` 区分复用；JOIN 必须等 CCCD 就绪；连接后重启可连接广播以支持第二人/旁观。`ROOM_STATE` v3 带两个座位的在线位/断线剩余秒数，同一 seq 的心跳仍刷新倒计时；v4 的 MOVE/PREVIEW 带方位与放兵行。
- UI 的“你/对方”只由 `gy_net_app_side_relation()` 的座位关系决定，不能从回合/阶段推断；输入门控另用 `local_turn()`。
- 自动化入口 `gy_net <cmd>` 只把动作排入队列，由 `gy_ui_tick()` 在 LVGL 主线程执行，和触摸走同一路径。
## 2. 代码索引

| 路径 | 作用 |
|---|---|
| `src/main.c` | 启动自检（core/input/ai/param/paramui/net/light）、LVGL/board 初始化、5ms 主循环。 |
| `src/geyi_board.c/.h` | 板级适配：IMU 20ms 采样线程、KEY2(PA43) 消抖、RGB；触摸由 LVGL 驱动；不读 KEY1/PA34。 |
| `src/geyi_input.c/.h` | 倾斜状态机：6°进/4°退、10°一档、200ms 稳定、方位比例、外围落点内缩；纯逻辑。 |
| `src/geyi_nav.c/.h` | 倾斜与屏幕按钮仲裁：互斥、谁先选、清除，避免同时操作。 |
| `src/geyi_game.c/.h` | 纯规则核心：棋盘/移动/合并/伤害/金币/胜负；`gy_preview`/`gy_commit`/`gy_place`/`gy_resign`。 |
| `src/geyi_ai.c/.h`、`geyi_ai_task.c/.h` | negamax+Alpha-Beta；独立线程、500ms 预览、取消/超时。 |
| `src/geyi_config.c/.h` | 10 项参数、单机/人机/联机三套参数集、范围/钳位/重置/应用到 `GyConfig`。 |
| `src/geyi_param_ui.c/.h` | 参数页两页状态机、模式切换、加减/翻页/重置；主机/桌面可测。 |
| `src/geyi_ui.c/.h` | 全部 LVGL 页面与交互：首页/模式/规则/准备/对局/参数/大厅/房间/结算；`gy_net` 测试入口。 |
| `src/geyi_feedback.c/.h` | RGB 反馈状态机（行动/胜负/平局闪灯）；无马达/音频。 |
| `src/geyi_net_proto.c/.h` | v4 线格式：12B 头、类型/长度/seq/小端、方位/放兵行、负载编解码、版本/范围校验；不依赖 OS/LVGL。 |
| `src/geyi_net_room.c/.h` | 房主权威状态机：席位/准备/STARTING/PLAYING/RESULT、快照、预览、10s 保留/回收、大厅 TTL。 |
| `src/geyi_net_link.c/.h` | 内存管道/内存链路，供主机测试和自检模拟双端。 |
| `src/geyi_net_ble.c/.h` | BLE 传输：GATT 服务端/客户端、广播/扫描、CCCD、分片、TX/RX 队列、`conn/gen` 生命周期。 |
| `src/geyi_net_app.c/.h` | 联机应用编排：ConnMap、JOIN/REJOIN/reclaim、状态/快照广播、旁观路由、UI 查询接口。 |
| `src/geyi_font_cn16.c/.h` | 简体中文 16px 子集（200 字）；新增中文字面量必须重新生成。 |
| `src/SConscript`、`project/proj.conf` | 源码自动收集；LVGL v9、BLE ACL=2、I2C3、RGB、8KB main 栈等配置。 |
| `tests/test_*.c` | 主机纯逻辑断言：规则/输入/倾斜/参数/参数页/AI/联机/平衡；入口见 `tests/README.md`。 |
| `evidence/`、`logs/` | 真机照片/串口/视频证据；Agent 会话导出。 |
| `manifest.yaml` | 教师评审交接：已知问题、物理验证记录、提交范围。 |
## 3. 核心文档索引

| 路径 | 作用 |
|---|---|
| `docs/README.md` | 文档总索引、阅读顺序、维护规则。 |
| `docs/07-量化测量报告.md` | 倾斜输入/AI 搜索的主机量化与复现方法。 |
| `docs/08-验收矩阵.md` | 一页式 REQ→设计→TC→证据矩阵，判断“是否真验收”。 |
| `docs/09-归档清单.md` | 封版/打包/个人最低能力验收。 |
| `docs/single-prototype.md` | 单机原型操作、固定试玩局面、规则变更记录。 |
| `docs/提案-参数配置与平衡性测试模块.md` | 参数子系统/平衡测试的模块分工提案。 |
| `docs/refs/third-party.md` | 第三方库/字体/许可。 |
| `docs/img/src/README.md` | 架构/数据流/状态机/联机拓扑 TikZ 源与生成物。 |
| `tests/README.md` | 主机测试目标、编译与运行方法。 |

## 4. 自动化测试（从烧录到模拟用户操作）

分层：L1 主机纯 C、L2 桌面 LVGL、L3 双板 BLE、L4 单板启动/物理。自动化 PASS 只证明软件路径；
真实像素/触摸/倾斜/长稳仍需 `evidence/` 证据，不能写成已验收。

### 4.1 主机与桌面

```bash
python tools/run_host.py                    # 6 组断言 + 联机 428+358；任一失败非 0
python tools/run_host.py --only=geyi-net-app-tests
python tools/verify_all.py --coverage       # 文档检查 + 主机 + 覆盖率 ≥70%
python tools/build_desktop.py --test        # 真 LVGL 桌面 --test（需 SDK + MSVC/gcc）
```

### 4.2 固件、烧录与双板全流程

```bash
# 1) 编译（先进入已 export 的 SDK 终端）；产物 project/build_sf32lb52-lchspi-ulp_hcpu/main.bin
python scripts/build_firmware.py

# 2) 烧录。flash_and_capture.py 只允许一个 CH340；两板都插着时按端口直接调用 sftool：
cd project/build_sf32lb52-lchspi-ulp_hcpu
~/.sifli/tools/sftool/0.2.5/sftool.exe -p COM7 -b 1000000 config sftool_param.json
~/.sifli/tools/sftool/0.2.5/sftool.exe -p COM8 -b 1000000 config sftool_param.json

# 3) 双板套件：等待 shell/UI，驱动 gy_net 命令走真实 LVGL 主线程；15 段全过才退出 0
python tools/net_board_test.py --port-a COM7 --port-b COM8
python tools/net_spectator_test.py --port-a COM7 --port-b COM8 --port-c COM5
```

- 套件覆盖：唯一 ID/规则页、空位/准备、完整对局到 RESULT、认输同步、秒退/连点/反复进出、
  WAIT/PLAYING 重启 10s 回收、瞬断 REJOIN、超时判负、退出/重进/关房、房主转移、过期房间、旁观只读。
- 日志落在 `evidence/net/20261109/board/suite_A_*.log`、`suite_B_*.log`；退出码 0 才是 PASS。
- 单板启动断言：`python tools/flash_and_capture.py --assert`（交互输入 `FLASH`，采集 120s）；
  `python tools/verify_all.py --board` 也只覆盖单板启动，不等于联机验收。
- 延迟预算：`python tools/net_latency_test.py --port-a COM7 --port-b COM8`；join/close/leave 超限即 FAIL。
- 三端旁观：`python tools/net_spectator_test.py --port-a COM7 --port-b COM8 --port-c COM5`（房主/房客/旁观者准备→对局→结算→第二局、断线倒计时、中途加入）；PC 第三设备仍可用 `pc_ble_peer.py --scenario third-peer|idle-link|burst`（Windows BLE+bleak，不进 CI）。
