# ADR-0013: 历史记录改用 NOR Flash + FlashDB KV 持久化

- 状态：**已采纳**（学生 2026-11-12 确认；替代 ADR-0010 的 RAM-only 结论）
- 日期：2026-11-12
- 决策人：郑瑞杭　协助：Claude Code / deepseek-flash[1m]
- 关联需求：REQ-602、REQ-603、REQ-605、REQ-606
- 关联设计：`docs/02`「历史记录与计时设计」；测试：`docs/03` TC-601~TC-606

## 背景

PR #23 交付的历史记录是 10 槽 RAM 环形缓冲，重启清空。组长要求改为 NOR +
FlashDB KV，使对局记录能跨重启保留；同时需要补联机房主/房客双端记录和胜方身份
（AI/真人/机主/对手）显示。

硬件约束：16 MB NOR 与固件共享、无 TF 卡座、无电池；FAL 对 `flash2` 的擦除块为
8 KB。板级现有分区在 `0x00DA8000` 之后到 16 MB 结束仍有空闲。

## 备选方案

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| A. FlashDB KVDB + FAL 专用分区 | 组长要求；有磨损均衡、KV 原子提交；SDK 有 `example/storage/flashdb/project/nor` 参考；应用只需一个 blob | 需要工程级 ptab/FAL 表；首次启动格式化；GC 擦除可能短暂停顿 | **采用** |
| B. 裸 FAL A/B 双缓冲 | 不依赖 FlashDB；格式可完全自控 | 需自写 CRC、双槽轮换、掉电/寿命逻辑；测试面更大 | 不采用 |
| C. DFS/ELMFAT 文件 | 可复用文件 API | 需要挂载文件系统、固件更大、写入更慢，且不是组长要求 | 不采用 |
| D. RAM-only（ADR-0010） | 最简单、无 Flash 风险 | 重启清空，不满足本次要求 | 被本 ADR 替代 |

## 决策

采用方案 A：

- 在工程级 `project/sf32lb52-lchspi-ulp_hcpu/ptab.json` 增加
  `KVDB_HISTORY_REGION`：offset `0x00DA8000`，size `0x8000`（32 KB，4 个 8 KB
  擦除块）；不修改 SDK `customer/boards` 骨架。
- 工程级 `project/sf32lb52-lchspi-ulp_hcpu/custom_mem_map.h` 的 HCPU FAL 表增加
  `history`，保留 `dfu`/`ble`；LCPU 继续使用 SDK 的 flash3 表。
- FlashDB KVDB：partition `history`，DB 名 `geyi_hist`，键 `records`；一个 blob
  保存完整 `GyHistoryStore`（magic/version/count/next/items[10]），不做动态分配。
- `src/geyi_history.c` 保持纯 RAM/可主机测试；`src/geyi_history_store.c` 只在
  `GEYI_WITH_RTTHREAD` 固件构建中初始化 FAL/FlashDB、加载和按 dirty 落盘。
- 启动顺序：`gy_history_selftest()` 先运行且只操作 RAM；随后
  `gy_history_store_init()` 加载 Flash；初始化放在 BLE NVDS 启动前，避免首次格式化
  与 BLE 争用 flash2。
- 写失败保留 RAM 数据并回退，不阻塞对局；约 1 s 退避重试，避免每 5 ms 写 Flash。
- 记录格式升到 v2：`reserved` 改为 `winner_identity`；容量仍为 10，满后淘汰最旧。

## 后果

- 正面：重启/掉电保留最近 10 局；FlashDB 负责磨损均衡和 KV 提交；主机/桌面仍可
  在没有 FlashDB 的环境测试纯逻辑。
- 代价：首次启动格式化 32 KB；GC 擦除可能造成短暂 UI 停顿；FlashDB DB 约 1.8 KB
  BSS；工程级 ptab 需要随 SDK 板级分区变化维护；时间戳仍是开机秒数（无 RTC 校准）。
- 需要同步：`docs/01` REQ-602/603/605/606、`docs/02` 历史设计、`docs/03` 测试计划、
  `docs/04` 测试报告、`docs/06` 风险台账、`manifest.yaml` 的决策/物理验证记录
  （根 manifest 由团队维护，需学生/组长确认后再改）。

## 待确认/边界

- 联机客方若丢失 RESULT 或重连时只收到 ROOM_STATE，可能拿不到精确 `result_reason`；
  列表仍能显示类型/胜方/身份，结束原因精度受网络路径限制，需在风险台账记录。
- 32 KB 分区按 FAL 8 KB 块为 4 块；若后续记录数增加或 GC 频繁，再评估扩到 64 KB。

## AI协作

agent 对比了 FlashDB KV、裸 FAL A/B 和 DFS 三种方案，读取 SDK 例程和板级分区后
建议 A；学生 2026-11-12 明确选择 FlashDB KV + FAL 分区、本地视角身份、容量 10。
agent 负责实现与主机/桌面/固件编译；板端掉电与双端联机记录由学生真机验证。
