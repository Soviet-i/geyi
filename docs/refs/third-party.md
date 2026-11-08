# 第三方资源清单

> 课程要求：允许复用开源项目/库/模型/素材，但必须注明来源、版本、许可与使用/修改情况。
> 本清单覆盖**产品固件**与**构建/文档工具**；不把 SDK 本体打包进仓库。
> 若后续新增库/字体/图片/音频/模型，必须在此登记，并保留许可文本。

## 1. 产品固件依赖

| 资源 | 版本/来源 | 许可 | 使用与修改 | 署名/许可文件 |
|---|---|---|---|---|
| SiFli SDK | v2.5.1；https://github.com/OpenSiFli/SiFli-SDK （国内镜像 gitee.com/SiFli/sifli-sdk） | Apache-2.0 | 使用 BSP/HAL/RT-Thread/LVGL 适配、构建系统；未修改 SDK 源码；`project/` 骨架保持原样 | 仓库根目录 `LICENSE-SiFli-SDK-Apache-2.0.txt` |
| LVGL | v9（随 SDK 提供） | MIT | 使用控件、字体、渲染；未修改 LVGL 源码；UI 代码在 `src/geyi_ui.c` | 见 SDK `external/lvgl_v9` 的 LICENSE |
| 思源黑体 / Source Han Sans SC | 随 SDK 提供 `SourceHanSansSC-Normal.otf`（Adobe/Google） | SIL OFL-1.1 | 用 `tools/generate_ui_font.py` 生成 16 px/4bpp 中文子集 `src/geyi_font_cn16.c`；未使用保留字体名 | `docs/refs/OFL-SourceHanSansSC.txt`；源字体版权归 Adobe |
| Noto Sans SC | Windows 系统/Google Noto | SIL OFL-1.1 | **仅用于生成文档设计图**（TikZ/XeLaTeX），不进入固件；PDF 嵌入字体子集 | https://fonts.google.com/noto/specimen/Noto+Sans+SC |
| 课程 `peripheral_lab` / `xiaozhi-sf32` 示例 | 课程提供（2026-10） | **待核实**（课程材料，未在仓库内复制） | 只参考外设初始化、LVGL、按键/IMU 的写法；未整体复制综合测试工程；具体复用点见 `docs/02` | 需向教师确认课程材料许可后再扩大复用 |
| RT-Thread | 随 SDK 提供 | Apache-2.0 | 内核/设备框架/线程/互斥量；未修改内核 | 见 SDK |
| CMSIS / ARM 头文件 | 随 SDK/工具链 | Apache-2.0 / BSD 类 | 编译期头文件；未修改 | 见工具链/SDK |

## 2. 构建与文档工具（不随产品分发）

| 工具 | 用途 | 许可 | 说明 |
|---|---|---|---|
| MinGW-w64 GCC 14.2.0 | 主机测试/覆盖率 | GPLv3 + runtime exception | 本机测试用；不链接进固件 |
| MSVC / Visual Studio 2022 | 桌面 LVGL 模拟器 | Microsoft 许可 | 仅本机桌面测试 |
| Python 3 / PyYAML / pyserial | 检查脚本、串口采集 | PSF / MIT / BSD | 脚本依赖，见 `requirements.txt` |
| TinyTeX / TeX Live（XeLaTeX + TikZ） | 生成设计图 PDF | LPPL/各宏包许可 | 仅文档构建 |
| PyMuPDF | PDF→PNG（`docs/img/src/build_figures.py`） | AGPL-3.0 或商业许可 | 仅本机把 PDF 转 PNG；不分发工具本身；如评审要求可改用 `pdftocairo` |
| GitHub Actions | CI | GitHub 服务条款 | 仅 CI |

## 3. 明确未使用的资源

- 未使用第三方图片、图标、音效、音乐、语音模型、端侧模型；准备页的人物/机器人图标是
  LVGL 基础对象自绘。
- 未使用云服务、在线 API、账号体系；产品不采集/上传用户数据。
- 未复制 SDK 源码进仓库；`project/` 只保留课程要求的工程骨架。

## 4. 维护规则

1. 新增依赖前先查许可证是否允许课程/公开仓库使用；不确定就写“待核实”，不要猜。
2. 复制代码片段/字体/图片时，在提交信息与本文登记来源、版本、许可、修改点。
3. 不把密钥、令牌、个人数据写入仓库或日志。
4. 结题时区分“自己的实现 / 改进 / 验证”，不要把复制的全部代码计为个人贡献。
