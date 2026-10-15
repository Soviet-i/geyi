# evidence/ —— 物理验证证据

存放“在真实硬件上验证过”的原始证据，按里程碑建子目录，例如 `evidence/M2/`。

## 可接受的证据

| 验证内容 | 证据形式 | 命名示例 |
|---|---|---|
| 烧录与启动 | 串口启动日志（文本，带时间戳） | `M2/boot_log_20261012.txt` |
| 传感器读数 | 串口输出文本 + 触发动作说明 | `M2/imu_shake_20261012.txt` |
| 屏幕/UI 效果 | 照片或短录屏（大文件放网盘，这里放链接文件） | `M3/ui_home.jpg` |
| 马达/震动 | 示波器 PWM 截图 + 手感文字描述 | `M3/motor_pwm.png` |
| 蓝牙/BT PAN 联网 | 连接成功日志或截图 | `M3/btpan_ping.txt` |
| 焊接/组装 | 关键步骤照片（前/后） | `M4/solder_motor_after.jpg` |

## 规则

1. 必须是**原始**材料，不要用修图或 agent 生成的“模拟输出”替代。
2. 文件名带日期，便于与 git 提交时间、开发日志交叉核验。
3. 每个证据文件在 `manifest.yaml` 的 `physical_verification[].evidence_path` 或 `test_results[].evidence_path` 中被引用。
4. 视频等大文件不要直接入库：把文件放网盘，在本目录写一个 `xxx.link.txt` 记录链接和提取码。
5. 做不到的验证（如没有示波器）如实写进 `known_issues`，不要编造证据。
