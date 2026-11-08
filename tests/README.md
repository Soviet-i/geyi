# 主机测试

这些测试直接链接 `src/` 的真实实现，不复制规则；用 gcc（MinGW/MSVC 均可）在 PC 上运行，
**不接触开发板**。板端行为必须另行物理验证。

## 运行

```bash
python tools/run_host.py                 # 构建并运行全部主机组（含联机 253 + app 70）
python tools/coverage.py --min 70        # 每模块行覆盖率
python tools/verify_all.py               # 文档检查 + 主机测试
python tools/net_board_test.py --port-a COM7 --port-b COM8  # 双板 BLE 自动冒烟
```

## 测试文件

| 文件 | 覆盖 | 主要断言 |
|---|---|---|
| `test_geyi.c` | 规则核心 `src/geyi_game.c` + 输入 | 初始化/预览/提交/移动顺序/合并/伤害/金币/胜负/确定性重放；26920 断言 |
| `test_input.c` | 输入状态机 `src/geyi_input.c` | 四向/滞回/稳定窗口/按键消抖/回中/回绕；60 断言 |
| `test_config.c` | 参数 `src/geyi_config.c` | 10 项范围/钳位/三类配置/重置/生成间隔/模拟一致性；2517 断言 |
| `test_param_ui.c` | 参数页状态 `src/geyi_param_ui.c` | 模式/分页/条目/增减/重置/应用；85 断言 |
| `test_ai.c` | AI `src/geyi_ai.c` | 合法动作/终局/避免自伤/不读 RNG/剪枝/取消/自对弈；62 断言 |
| `test_balance.c` | 平衡扫描 `tools/geyi_balance.c` | 参数网格/固定 seed/CSV 可复现；10 断言 |
| `test_net.c` | 联机协议/房间/大厅/内存管道 `src/geyi_net_*.c` | 编码/版本/seq/快照/准备/开局/落子/断线/重连/旁观/大厅 TTL/空位换座；253 断言 |
| `test_net_app.c` | 联机 app 路由 `src/geyi_net_app.c` | 换座颜色、state+snapshot 同 seq、MOVE_REQ 幂等、对局 LEAVE、重复 JOIN、满员；70 断言 |

`tools/net_board_test.py` 是**两块真板**的自动冒烟，不是主机测试；它通过串口 shell
把 `gy_net ...` 动作排入 UI 主线程，覆盖发现/连接/服务发现/CCCD/加入/准备/双向落子。

## 新增测试

1. 在 `tests/` 新建 `test_xxx.c`，用 `CHECK(...)` 宏断言；`main()` 返回非 0 表示失败；
2. 在 `tools/run_host.py` 的 `targets` 增加源码列表与可执行名；
3. 在 `docs/03` 写对应 TC，在 `docs/04` 记录实际输出；
4. 运行 `python tools/run_host.py`，确认全 PASS；
5. 提交 `test(scope)`，正文写命令、期望、实际。

约束：

- 固定种子/固定局面，保证可复现；
- 失败保留原始输出，修好后新增重测记录；
- 不要用“复制一份规则”的测试；必须链接真实实现；
- 不要为了通过放宽断言或改期望；需求变更先改 `docs/01`/`docs/03`。
