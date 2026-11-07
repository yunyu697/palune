# Palune 开发状态（断点）

> 自动推进的续跑点。每完成一个可验证节点更新本文件；撞到单轮上限时以本文件为准。

更新时间：2026-11-07 10:50（Asia/Shanghai）
当前里程碑：**M2（三主题验收矩阵）→ 即将进入 M3**
版本：`1.0.0-alpha4`

## 基线

| 项 | 状态 |
|---|---|
| 基线 commit | `f7fe72f`（已推送 `origin/main`，工作区干净） |
| CI（f7fe72f） | **三平台全绿**：版本/分层/覆盖率/Windows/Linux/macOS 全部 success；macOS 冒烟 20/20 样本 PASS |
| macOS SIGBUS 根因 | lldb 栈：`mpv_create → AppHub.init → RemoteCommandCenter.init → AppHub.getIcon → NSImage(data:) → ImageIO PNGReadPlugin`，PC 跳到 `0xbad4007`。根因是 `DYLD_LIBRARY_PATH` 含 `$(brew --prefix)/lib`，Homebrew 的 libpng/libjpeg 抢占了系统 ImageIO 的解码插件；收窄为 `libsodium/lib:mpv/lib` 后消失 |
| CI（1d5d306） | 仅 macOS 失败（上述 SIGBUS），其余全绿 |
| 本机 8h 浸泡 | 后台运行中（PID 13516，起于 08:23，预计 16:24 出 `qa/runtime/m2-soak-8h.json`） |

## M1 遗留收口

- **CI 三平台**：已全部转绿（含 macOS 20/20 冒烟样本）；此前样本生成与 locale 问题均已修复。
- **覆盖率**：`单测覆盖率` 作业 70% 门槛已恢复绿色（补 `tests/unit/tst_actions.cpp` 与 Settings 续播用例）。
- **1 小时压测**：已完成 1/3 轮，`docs/evidence/m1-soak.json`，RSS 218MB→190MB（-28.3MB/h），无增长趋势；完整 8h 数据由 M2 soak 补齐。
- macOS/Linux headless 冒烟：Linux/macOS 均绿。

## Windows 安装包三问题（M1 实测反馈）

| 问题 | 根因与修复 | 证据 |
|---|---|---|
| 安装后无法运行 | Qt 运行库/QML/平台插件/VC 运行库/libmpv/libsodium 未随包；安装脚本改为按依赖清单打包并部署 `platforms/qwindows.dll` | `docs/evidence/windows-install-launch.json`（launch.ok=true） |
| 开始菜单/桌面无快捷方式 | 安装器未创建；改为 AllUsers 开始菜单 + Public 桌面 + 卸载项 | `windows-install-evidence.json`：3 个快捷方式均存在且指向安装目录 |
| 许可协议乱码 | LicenseData 编码不一致；改为 RTF 中文许可 | `docs/evidence/license-rtf-alpha4.png` |

- 重新出包并完成真实安装 → 启动 → 卸载全流程，综合判据 `verdict = PASS`（安装退出码 0、文件清单齐全、卸载后目录/快捷方式均清除）。

## 品牌卫生（长期约束）

- 规则：仓库内不得出现外部播放器品牌/产品名（不区分大小写与常见变体），后续提交信息/PR 同样适用。
- 扫描：`python tools/ci/check_brand_terms.py`（与 CI 同一入口）→ **0 命中**；项目文档目录用同规则脚本扫描亦 0 命中。
- 首次 CI 回归：本文件旧版把关键词写进扫描命令示例，被 lint 自命中 1 处；已改为脚本入口写法，不再在仓库文本中出现品牌词。
- 改前命中：项目文档 `docs/ARCHITECTURE.md` 7 处（已改写为本项目设计表述）；仓库内 0 处。
- 固化：新增 `tools/ci/check_brand_terms.py` 并接入 CI `分层静态检查` 作业，防止回归。

## M2 验收矩阵（9 项）

| # | 判据 | 状态 |
|---|---|---|
| 1 | 36 界面评审基线签字 | **待用户评审**（截图已齐） |
| 2 | 六组缩放无溢出/截断/错位 | **PASS**：480×270、1280×720、4K100%、1440×900@125/150/200%，各 36/36；`m2-scale-matrix.json` |
| 3 | 40 个 P0 动作三主题可绑定可达、默认冲突 0、焦点顺序 + 3 套纯键盘用例 | **PASS**：`m2-actions.json`（40/40、冲突 0、A/B/C 各 3 用例全过、控件 14/13/11） |
| 4 | A OSC / B dock / C 胶囊唤出隐藏 ≤100ms；4K60 UI 帧 <16.7ms | **PASS**：`m2-ui-perf.json`（show 85.6/99.6/84.9ms，hide 31.5/20.0/99.1ms，renderP95 ≤0.85ms） |
| 5 | 50 次跨主题切换状态保持 | **PASS**：`m2-stateswitch.json` |
| 6 | 自动降级阈值 + 配置回退 | **PASS**：`m2-compact.json`、`m2-config-fallback.json`、`m2-compact-baseline.json`（640×360 各主题 36/36） |
| 7 | 强杀 10 次续播 ±1s；8h 浸泡 | 强杀 **PASS**：`m2-resume-kill.json`（10 轮，delta 0~0.067s）；8h 浸泡进行中 |
| 8 | 窗口行为 20 项清单 | **部分通过**：`m2-window.json` 20 项，18 PASS / 2 NOT_VERIFIED（跨屏移动、运行中 DPI 切换，需真机）；Alt-Tab/任务栏、Snap 样式、全屏、最小化恢复、几何持久化均 PASS |
| 9 | 中/英 i18n + 无硬编码文案 lint | **PASS**：`m2-i18n.json`；QML 125 处 qsTr、10 上下文 203 条翻译全完成，硬编码 0；中/英各 37 张截图（`shots/m2-i18n-zh|en`） |

## 下一步（按顺序）

1. 等待 8h 浸泡结果（约 16:24），判定 RSS 漂移 ≤50MB/h、崩溃 0，补 M2 #7 完整结论。
2. M2 #1 需用户签字的 36 界面评审基线（截图位于 `docs/evidence/shots/m2-baseline`、`m2-scale`）；签字后 M2 收口。
3. 同时启动 M3 前期工作（读 `MILESTONES.md` M3 判据，拆解首个可验证节点）。
4. M2 全项达标后：版本递进（alpha4→alpha5，`python tools/version/check_version.py`）、README/证据更新、提交推送，正式进入 M3。
5. M2 #8 剩余真机项：多屏移动、运行中 DPI（125/150/200%）切换需用户环境；自动化部分已完成。

## 阻塞 / 需用户

- M2 #1 36 界面评审签字需用户确认（截图位于 `docs/evidence/shots/m2-baseline`、`m2-scale`）。
- 签名证书/私钥、域名、真机多屏/DPI 环境：按未签名、NOT_VERIFIED 处理，不阻塞推进。
