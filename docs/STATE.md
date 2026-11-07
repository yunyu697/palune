# Palune 开发状态（断点）

> 自动推进的续跑点。每完成一个可验证节点更新本文件；撞到单轮上限时以本文件为准。

更新时间：2026-11-07 08:55（Asia/Shanghai）
当前里程碑：**M2（三主题验收矩阵）**
版本：`1.0.0-alpha4`

## 基线

| 项 | 状态 |
|---|---|
| 基线 commit | `5ddcbbb`（已推送 `origin/main`） |
| 工作区 | 有未提交改动：M2 动作注册表、品牌卫生 lint、macOS 样本修复 |
| CI 42317966413（5ddcbbb） | Windows/Linux/覆盖率/分层/版本 通过；**macOS 红**：样本生成时 libopus 拒绝 `-q:a` |
| 本机 8h 浸泡 | 后台运行中（PID 13516，起于 08:23，预计 16:24 出 `qa/runtime/m2-soak-8h.json`） |

## M1 遗留收口

- **CI 三平台**：Windows/Linux/覆盖率/分层/版本已绿；macOS 失败点已定位为 `tools/qa/gen_samples.py` 的 `libvorbis→libopus` 回退仍带 `-q:a 3`，已改为显式 `-b:a 96k`，待推送复跑。
- **覆盖率**：CI `单测覆盖率` 作业 PASS（门槛达标）。
- **1 小时压测**：已完成 1/3 轮，`docs/evidence/m1-soak.json`，RSS 218MB→190MB（-28.3MB/h），无增长趋势；完整 8h 数据由 M2 soak 补齐。
- macOS/Linux headless 冒烟：Linux 已绿；macOS 因样本生成失败未进入冒烟，修复后复跑。

## Windows 安装包三问题（M1 实测反馈）

| 问题 | 根因与修复 | 证据 |
|---|---|---|
| 安装后无法运行 | Qt 运行库/QML/平台插件/VC 运行库/libmpv/libsodium 未随包；安装脚本改为按依赖清单打包并部署 `platforms/qwindows.dll` | `docs/evidence/windows-install-launch.json`（launch.ok=true） |
| 开始菜单/桌面无快捷方式 | 安装器未创建；改为 AllUsers 开始菜单 + Public 桌面 + 卸载项 | `windows-install-evidence.json`：3 个快捷方式均存在且指向安装目录 |
| 许可协议乱码 | LicenseData 编码不一致；改为 RTF 中文许可 | `docs/evidence/license-rtf-alpha4.png` |

- 重新出包并完成真实安装 → 启动 → 卸载全流程，综合判据 `verdict = PASS`（安装退出码 0、文件清单齐全、卸载后目录/快捷方式均清除）。

## 品牌卫生（长期约束）

- 规则：仓库内不得出现外部播放器品牌/产品名（不区分大小写与常见变体），后续提交信息/PR 同样适用。
- 扫描：`rg -i -n --hidden --no-ignore -g '!/.git' -g '!build-*/**' "potplayer|pot player|daum|iina|lsony" .` → **0 命中**；项目文档目录同命令 → **0 命中**。
- 改前命中：项目文档 `docs/ARCHITECTURE.md` 7 处（已改写为本项目设计表述）；仓库内 0 处。
- 固化：新增 `tools/ci/check_brand_terms.py` 并接入 CI `分层静态检查` 作业，防止回归。

## M2 验收矩阵（9 项）

| # | 判据 | 状态 |
|---|---|---|
| 1 | 36 界面评审基线签字 | **待用户评审**（截图已齐） |
| 2 | 六组缩放无溢出/截断/错位 | **PASS**：480×270、1280×720、4K100%、1440×900@125/150/200%，各 36/36；`m2-scale-matrix.json` |
| 3 | 40 个 P0 动作三主题可绑定可达、默认冲突 0、焦点顺序 + 3 套纯键盘用例 | **PASS**：`docs/evidence/m2-actions.json`（40/40、冲突 0、A/B/C 各 3 用例全过、控件 14/13/11） |
| 4 | A OSC / B dock / C 胶囊唤出隐藏 ≤100ms；4K60 UI 帧 <16.7ms | 未做 |
| 5 | 50 次跨主题切换状态保持 | **PASS**：`m2-stateswitch.json` |
| 6 | 自动降级阈值 + 配置回退 | **PASS**：`m2-compact.json`、`m2-config-fallback.json`；紧凑形态截图基线未做 |
| 7 | 强杀 10 次续播 ±1s；8h 浸泡 | 强杀未做；8h 浸泡进行中 |
| 8 | 窗口行为 20 项清单 | 未做（真机项标注 NOT_VERIFIED） |
| 9 | 中/英 i18n + 无硬编码文案 lint | 未做 |

## 下一步（按顺序）

1. 推送 macOS 样本修复，复跑 CI 至三平台全绿。
2. M2 #4：控制层唤出/隐藏 ≤100ms 实测 + 4K60 UI 帧时间测量（证据 JSON）。
3. M2 #6 补充：紧凑断点下 A/B/C 截图基线。
4. M2 #7：10 次硬杀续播 ±1s 脚本与证据；等待 8h 浸泡结果。
5. M2 #9：中/英 i18n + 硬编码文案 lint。
6. M2 #8：窗口行为 20 项（可自动项自动执行，真机项标 NOT_VERIFIED）。
7. M2 全项达标后：版本递进、README/证据更新、提交推送，进入 M3。

## 阻塞 / 需用户

- M2 #1 36 界面评审签字需用户确认（截图位于 `docs/evidence/shots/m2-baseline`、`m2-scale`）。
- 签名证书/私钥、域名、真机多屏/DPI 环境：按未签名、NOT_VERIFIED 处理，不阻塞推进。
