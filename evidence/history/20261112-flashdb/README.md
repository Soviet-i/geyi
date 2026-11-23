# 历史记录 NOR + FlashDB 修订（2026-11-12，软件段）

本目录只包含软件构建证据；**板端持久化/联机双端/四列布局尚未验证**。

## 首次持久化构建（已被下方 AI 修复构建取代，保留历史）

- SDK：SiFli SDK v2.5.1，板型 `sf32lb52-lchspi-ulp`（HCPU `_hcpu`）。
- 命令：`python scripts/build_firmware.py`（使用 SDK 管理环境的 scons/ARM GCC）。
- 结果：`main.bin` **1,098,196 B**。
- SHA256：`12678c258038c1f9fb6ea0da3aad1875c2ca84b383fd0c1bac4cb35d22d387cc`
- 完整日志：`firmware_build.log`。
- 桌面测试日志：`desktop-test.log`（初次）、`desktop-test-ai-fix.log`（静态栈失败尝试）、
  `desktop-test-ai-revert.log`（回退）、`desktop-test-ai-early8k.log`（8 KB 预创建）、
  `desktop-test-lazy8k.log`（当前按需创建）；截图仍由 `tools/build_desktop.py` 输出到
  `build/desktop/`，未复制为提交证据。

## AI 回归修复构建（2026-11-12 后续；含一次失败尝试）

- 问题：人机 AI 回合无响应；人机 AI 回合点认输被判成 AI 认输。
- 第一次尝试：AI 线程改静态 16 KB 栈，板端 `free` 显示堆只剩约 4.7 KB，
  随后 `mbox` 线程触发 `rt_assert_handler`，触控无响应。该尝试已回退，日志
  `firmware_build_ai_fix.log`（1,098,300 B）保留为失败记录。
- 第二次修复：恢复 `rt_thread_create`，AI 线程只在第一次 AI 回合按需创建，栈从
  16 KB 降到 8 KB（搜索工作区本身是静态对象），加创建失败日志；人机认输固定由
  人认输、判人负；AI 思考/预览期间不写 FlashDB。
- 第三次调整：移除启动预创建，恢复第一次 AI 回合按需创建 8 KB；同时修复联机建房
  流程：从大厅创建房间前先停 BLE 扫描并等待 30 ms，避免扫描仍 active 时创建广播。
- 第四次调整：板端日志继续出现 `adv started status=67`（GAP_ERR_COMMAND_DISALLOWED）
  每秒重启。`ADV_STARTED` 失败时清 `g_adv_inited`、置 `g_adv_rebuild`，下一次强制
  `stop -> delete -> init -> start`，不再对同一 IDLE 实例重复 start。
- 第五次调整：板端 `list_thread` 确认没有 `gy_ai`，且堆只剩约 14 KB，8 KB 堆栈仍
  创建失败；把 AI 栈改为 PSRAM 内的静态 16 KB 栈。
- 第六次修正：C 数组的 section 属性把 `.RW_PSRAM1` 链接成 PROGBITS/LOAD，
  `objcopy` 生成从 Flash 到 PSRAM 的 1.3 GB `main.bin`，sftool 超时。
  改为 `src/geyi_ai_stack.S` 的 NOBITS 段（`.l2_ret_bss_ai_stack,"aw",%nobits`），
  `rt_thread_init` 使用该栈；`.RW_PSRAM1` 不再 LOAD，实际 `main.bin` 回到
  **1,098,428 B**。
- 当前 `main.bin` SHA256
  `c2443f5a1be9afb2d023027db0f436c7abbfd0ac0da6cf53b983ccd345ceb022`；
  完整日志：`firmware_build_nobits_fix.log`。
- 该构建尚未烧录；上板后需重点测试：触控、两块板大厅扫描/建房、连接稳定性、
  AI 蓝/红先手、AI 回合点认输判人负、历史记录仍持久化。
- 旧 1,098,196 B 构建板端观察：断电重上电曾停在 `SFBL` BootLoader；CodeKit
  DTR/RTS 复位后启动，串口 `history_store=PASS count=1`，学生确认历史仍在。
  后续上电前关闭串口监视器、不要按住 KEY1/BOOT；这不是完整板端通过。

## 分区/链接检查

- 构建日志确认使用工程级
  `project/sf32lb52-lchspi-ulp_hcpu/ptab.json`，不是 SDK `customer/boards` 原表。
- 生成的 `ptab.h`：
  - `KVDB_HISTORY_REGION_OFFSET=0x00DA8000`
  - `KVDB_HISTORY_REGION_SIZE=0x00008000`（32 KB）
- 生成的 HCPU `custom_mem_map.h` 含 `dfu`/`ble`/`history` 三个 FAL 表项；
  `history` 使用 `NOR_FLASH2_DEV_NAME`。
- LCPU `custom_mem_map.h` 仍使用 `NOR_FLASH3_DEV_NAME`，未被 HCPU 表污染。
- `src/geyi_history_store.o` 已编译并链接。

## 软件验证

- 主机：`python tools/run_host.py` 全 PASS；历史 84 断言、net-app 360 checks。
- 桌面：`python tools/build_desktop.py` 全 PASS（历史四列/详情/清空/单机记录）。
- 字库：300 个非 ASCII 字形，`--check` PASS。
- 以上不等于板端通过。

## 待学生真机验证（必须回传证据）

1. 烧录最新修复构建 1,098,428 B 固件，串口确认 `history=PASS timer=PASS`，
   `history_store=PASS count=0`（首次）或已有条数。
2. 单机/人机/联机（两块板，房主+房客各一条）完成后，历史可见。
3. 断电重启后记录仍在；清空后重启仍为空。
4. 列表表头“类型 时长 胜方 胜方身份”和身份值（AI/真人/机主/对手/单机空）
   的照片或屏摄。
5. 串口日志/照片放入本目录，并在 `docs/04`/`docs/06`/manifest 登记。
