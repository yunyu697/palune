# 里程碑复审（M0–M3）GAP-AUDIT

- 审计日期：2026-11-08（Asia/Shanghai）
- 审计范围：M0、M1、M2、M3（含 alpha5–alpha10 期间的补丁子项）
- 当前基线：工作区 `VERSION = 1.0.0-alpha10`，另含 11-08 16:01 后落地的 M4 媒体库代码；**alpha9 不是当前状态**
- 判定口径（四选一）：
  - **DONE**：有实现，且能给出文件/截图/日志/测试名作为证据
  - **PARTIAL**：做了一部分，写清做到哪一步、缺什么
  - **NOT DONE**：根本没做
  - **NOT_VERIFIED**：代码或产物存在，但无法验证实际行为，写清为什么
- 纪律：禁止用“整体 PASS”覆盖单项缺失；NOT DONE 不得写成 PARTIAL；此前报过 PASS 但实际缺项的，单列追责。
- 本轮只读源码与已有证据文件：**未跑构建、未启动应用截图、未执行 git 操作**。

## 0. 审计前提（必须先看）

| 事实 | 说明 | 影响 |
|---|---|---|
| `MILESTONES.md`、`DESIGN.md`、`ARCHITECTURE.md`、`prototype-*.html` 在当前工作区**不存在** | 全仓 `*.md` 仅有 README / NOTICE / docs/STATE.md / docs/RELEASES.md / docs/MOUSE-INTERACTION.md / docs/evidence/*。`STATE.md` 明确记录 `ARCHITECTURE.md` 曾被改写，说明它曾存在，但现在缺失 | 无法逐字读取原始验收条目与设计约束。本复审只能用 `STATE.md`、`README.md`、`docs/evidence/*` 反推条目，并在每条标注来源。**这本身是复审的第一个缺口** |
| 本轮未执行 `git log` / `git status` | 用户明确要求不执行 git 操作 | 不能断言这些文档“从未进过 git”；只能断言“当前工作区缺失” |
| 本机为 Windows 单显示器（3840×2160@200%），无 WiX，无 macOS/Linux 机器 | 现有证据均在该环境产出 | 三平台、真实跨屏 DPI、真实 Aero Snap 手势、MSI、干净虚拟机只能记 NOT_VERIFIED/PARTIAL |
| 证据以仓库内文件为准 | `docs/evidence/` 与 `shots/` 内的 JSON/PNG/LOG；CI 产物（`m1-smoke-*`、`coverage`）只在 GitHub 保留期内有效，未下载归档 | 依赖 CI 产物的条目只能以 CI 作业名 + STATE 记录作证，无法离线复核原始数值 |

> 本文件不补造任何“原文档条款”。下文每条都标注来源：README 段落 / STATE 章节 / 用户复审要求 / 源码与证据文件。

## 1. M0 总表

| ID | 条目（来源） | 结论 | 证据位置 | 缺失说明 | 优先级 |
|---|---|---|---|---|---|
| M0-01 | 双窗架构：VideoWindow + OverlayWindow（README「已完成 M0」） | DONE | `src/window/VideoWindow.cpp`、`OverlayWindow.cpp`、`WindowController.cpp`；`docs/evidence/m0-report.json`（应用窗口数 1） | — | — |
| M0-02 | libmpv 动态加载，`vo=gpu-next` + `gpu-context=d3d11` + `hwdec=d3d11va`（README M0） | DONE | `src/mpv/MpvClient.cpp`；`docs/evidence/m1-smoke.json` 20/20 | — | — |
| M0-03 | 最小能力：打开文件/播放暂停/拖动进度/音量/全屏/退出（README M0） | DONE | `src/app/AppController.cpp`、`WindowController::action`；`m1-smoke.json` 20/20 | — | — |
| M0-04 | 设置页更新模块：检查更新/打开主页常驻、镜像切换、SHA-256 + Ed25519（STATE alpha1） | DONE | `qml/UpdateSection.qml`（常驻“检查更新/打开主页/镜像”）；`src/update/*`；`report-e2e-full.json`、`report-check-badsig.json`、`report-mirror-poison.json`、`report-mirror-404.json`、`UPDATE-CHAIN-ALPHA6.md` | 签名使用的仍是仓库内测试密钥（见 M1-18），不是本条缺失 | — |
| M0-05 | 版本号规则校验接入 CI + 三平台构建（README M0） | DONE | `tools/version/check_version.py`；`.github/workflows/ci.yml`（版本号校验 + Windows/macOS/Linux matrix）；README CI run `runs/42216916696` | — | — |
| M0-06 | 全屏切换 20 次：几何偏移 ≤1 帧、不黑（STATE alpha1） | DONE | `docs/evidence/m0-report.json`（同步最大 6.284ms，亮度 20/20 非黑） | — | — |
| M0-07 | DPI 100/150/200、跨屏拖动零偏移（STATE alpha1） | **PARTIAL** | `m2-scale-matrix.json`（125%/150%/200% 各 36 张全 PASS，方法为 `QT_SCALE_FACTOR` 模拟）；`ui-alpha10-matrix.json`（100%/150% 实拍）；`m2-window.json` 中 `dpi_live_switch` 为 NOT_VERIFIED | 缩放矩阵已覆盖 100–200%，但**没有第二显示器**；运行中真实 DPI 切换、跨屏拖动仍未验证。合成缩放不能替代真实多屏 | P3 |
| M0-08 | Alt-Tab 只有 1 个应用窗口、恢复画面正常（STATE alpha1） | DONE | `m0-report.json`（`altTab=PASS`，窗口数 1，最小化/恢复 10 轮非黑）；`m2-window.json`（overlay appwindow / video toolwindow exstyle） | 仅验证“恢复非黑”；“暂停态切走再切回残留”在 alpha10 暴露并修复，见 M3-10③ | — |
| M0-09 | Snap 四边贴边 + 最小化恢复零偏移（STATE alpha1） | **PARTIAL** | `m0-report.json`（`snap=PASS`）；`m2-window.json`（`snap_geometry` 0,0 960×1032） | 用 `SetWindowPos`/程序化几何模拟系统贴边，**不是鼠标拖拽手势**；真实拖拽贴边未验证 | P3 |
| M0-10 | macOS/Linux 渲染路径（README M0 三平台） | **NOT_VERIFIED** | `src/mpv/MpvClient.cpp` 保留 macOS/Linux 分支；CI 三平台构建矩阵 | 从未在 macOS/Linux 真机运行；CI 只证明可构建（M1-16 的冒烟另计） | P2 |
| M0-11 | 窗口拖动 / 四边四角缩放（alpha5 修复） | DONE | `docs/evidence/window-drag-resize.json`（拖动 200×100、缩放 +300） | 程序化注入输入，不是真实指针；但拖动/缩放路径本身可用 | — |
| M0-12 | 无边框窗口圆角（alpha5） | DONE | `docs/evidence/window-radius.json`（A/B/C = 27/20/41 物理像素） | — | — |
| M0-13 | 空闲视频子窗不露白块（alpha5） | DONE | `window-radius.json`、`shots/alpha5-*` 截图矩阵 | — | — |

**范围外（非验收项，不计入四选一）**：位图皮肤、3D/VR、软解滤镜矩阵、8K 承诺——README「明确不做」；逐项确认未实现，符合约束。

## 2. M1 总表

| ID | 条目（来源） | 结论 | 证据位置 | 缺失说明 | 优先级 |
|---|---|---|---|---|---|
| M1-01 | 四层主题：tokens → components → layouts → ThemeRegistry/LayoutHost（README M1） | DONE | `qml/tokens`、`qml/components`、`qml/layouts/*`、`src/theme/*` | — | — |
| M1-02 | VideoHost 在布局树外，切主题不触碰视频承载窗（README M1） | DONE | `LayoutHost::layoutContainsVideoHost()`；`m1-theme-switch.json`（切换前后 `startFileDelta`/`endFileDelta` 为 0） | — | — |
| M1-03 | 主题持久化 + 缺键/非法/资源缺失回退 A（README M1） | DONE | `src/theme/ThemeRegistry.cpp`；`m2-config-fallback.json` 5 场景；`tst_theme` | — | — |
| M1-04 | 最小 UI：打开/拖拽/播放暂停/seek/音量/速度/音字幕轨/全屏/退出/最近/三布局（README M1） | DONE | `qml/Main.qml`、`qml/layouts/*`、`src/window/WindowController.cpp`；`m1-smoke.json` | — | — |
| M1-05 | settings.toml 原子读写、保留未知键、窗口几何恢复（README M1） | DONE | `src/settings/Settings.cpp`（`QSaveFile`）、`Toml.cpp`；`tests/unit` | — | — |
| M1-06 | 内容源抽象：五组接口 + MediaItem/UserState + 适配器注册表（README M1） | DONE（抽象层） | `src/media/MediaServerAdapter.h`、`MediaLibraryService.h`、`Types.h`；`registerAdapter/unregisterAdapter/adapters()` | 抽象层齐全，但注册表内**没有任何适配器实现**；本地目录扫描在 M4 落地（见 M3-07），不是本条的接口缺口 | — |
| M1-07 | 分级日志 / 崩溃面包屑 / 版本信息页（README M1） | DONE | `src/diag/*`；`qml/SettingsPanel.qml` 关于页；`diag.logLevel` | — | — |
| M1-08 | 更新清单 payload + 分离签名信封（README M1） | DONE | `src/update/UpdateManifest.*`；`docs/evidence/UPDATE-CHAIN-ALPHA6.md`、`update-chain-alpha6.json` | — | — |
| M1-09 | 命名与标识冻结（appId、`%APPDATA%\Palune`、日志前缀、`palune://`）（README M1） | DONE | `src/core/AppPaths.*`、`CMakeLists.txt` | — | — |
| M1-10 | 20 样本 × 5s 冒烟（Windows）（README M1） | DONE | `docs/evidence/m1-smoke.json` 20/20 | — | — |
| M1-11 | 冷启动 ≤800ms（README M1） | **PARTIAL** | `m1-perf.json` 574ms（alpha2）；`install-launch-alpha8.json` **1466ms**；`qa/runtime/alpha10/install-launch.json` **2116ms**，字段 `windowReadyWithin800ms=false` | 历史基线达标，alpha8/alpha10 安装包均超 800ms，且 alpha10 比 alpha8 又慢约 650ms。**这是一个未被里程碑收口拦住的回归** | P3 |
| M1-12 | 空闲内存 ≤300MB（README M1） | DONE | `m1-perf.json` 94.1MB | — | — |
| M1-13 | 1080p 播放内存 ≤500MB，丢帧/错时帧 0（README M1） | DONE | `m1-perf.json` 204.5MB、`frameDrops=0`、`mistimedFrames=0` | — | — |
| M1-14 | 主题切换 10 次 ≤300ms、不断播、无黑帧（README M1） | DONE | `m1-theme-switch.json`（最大 104.1ms，全部非黑，音画间隙 0） | — | — |
| M1-15 | 覆盖率 ≥70%（README M1） | DONE | `.github/workflows/ci.yml` 覆盖率作业（`gcovr --fail-under-line 70`，上传 `coverage`）；`STATE.md`「覆盖率作业 70% 门槛已恢复绿色」 | 本地无覆盖率报告文件，CI 产物未归档；离线复核需重跑 CI。结论依据是**带硬门槛的 CI 作业通过**，不是某份本地报告 | — |
| M1-16 | 20 样本 × 3 OS 冒烟（macOS/Linux 各 ≥19/20）（README M1） | DONE | `ci.yml` 三平台 `--m1-smoke` 并上传 `m1-smoke-{Windows,Linux,macOS}`；`STATE.md`「三平台全绿，macOS 冒烟 20/20」 | 原始 JSON 未下载归档；离线复核需重跑 CI | — |
| M1-17 | 1h × 3 循环 RSS 无持续增长（README M1） | **PARTIAL** | `m1-soak.json`（60 分钟、RSS 189.9MB、`PASS`） | 只完成 1/3 轮；缺 2 轮，不能证明长时间无泄漏 | P3 |
| M1-18 | 签名与更新通道：可签名、可回退（STATE alpha6） | **PARTIAL** | `UPDATE-CHAIN-ALPHA6.md`；`resources/trust/update.pub`、`test-update.pub`；`tools/update/test-key.pem` | 使用仓库内**测试**私钥/公钥（`palune-test`）；没有生产离线密钥、真实域名与回退通道。发 stable 前必须替换 | P1 |
| M1-19 | 三平台安装包 + 干净虚拟机（README M1） | **PARTIAL** | `STATE.md` alpha4 Release：Windows exe + **MSI**、macOS arm64 dmg、Linux AppImage/deb/rpm 均已产出并有 SHA-256；`qa/runtime/alpha10/windows-install-evidence.json`（Windows NSIS 真装到 `C:\PaluneVerify`、快捷方式/文件树/卸载）；`docs/evidence/install-verify-alpha8.json` | 安装包已三平台产出；缺的是：**干净虚拟机**未验证；macOS/Linux 包未实际安装启动；MSI 只产出未走安装流程 | P2 |
| M1-20 | 单元测试（README M1） | DONE | `ctest -C Release` 7/7（STATE alpha9）；`tests/unit/tst_*.cpp` | 测试数量有限，覆盖深度由 M1-15 的覆盖率门槛兜底 | — |
| M1-21 | CI 分层静态检查 + 三平台构建（README M1） | DONE | `ci.yml`：版本号、分层、品牌卫生、硬编码文案、i18n 检查；三平台 matrix | — | — |

## 3. M2 总表

| ID | 条目（来源） | 结论 | 证据位置 | 缺失说明 | 优先级 |
|---|---|---|---|---|---|
| M2-01 | 三主题 × 12 界面 = 36 张 2880×1800 基线（STATE M2 #1） | DONE | `m2-ui-baseline.json`、`shots/m2-baseline/` | — | — |
| M2-02 | 缩放/分辨率矩阵 6 组（480×270 → 4K@200%）（STATE M2 #2） | DONE | `m2-scale-matrix.json`（每主题每配置 36 张，共 216 张）；`shots/m2-scale/` | — | — |
| M2-03 | 窄窗口设置面板（横向页签 + 可滚动）（STATE M2 #2 修复项） | DONE | `qml/SettingsPanel.qml` `narrow` 分支；`m2-scale-matrix.json` 480×270 设置页截图 | — | — |
| M2-04 | 布局 B 左坞避让 / 低高度压缩（STATE M2 #2 修复项） | DONE | `qml/layouts/LayoutB/LayoutB.qml`；`m2-scale-matrix.json` | — | — |
| M2-05 | 50 次主题切换状态保持（STATE M2 #3） | DONE | `m2-stateswitch.json`（50 次，位置/音量/面板宽保持） | — | — |
| M2-06 | 紧凑模式：断点 560、可配、±24 滞回、60ms 防抖、10Hz 抖动 0 切换（STATE M2 #4） | DONE | `src/ui/CompactController.cpp`；`m2-compact.json`、`m2-compact-baseline.json` | — | — |
| M2-07 | 配置回退矩阵 5 场景（STATE M2 #7） | DONE | `m2-config-fallback.json` | — | — |
| M2-08 | 窗口回归 20 项（STATE M2 #6） | **PARTIAL** | `m2-window.json`（20 项，`failures=0`，但跨屏移动、运行中 DPI 2 项 NOT_VERIFIED） | 18/20 有结论；跨屏与真实 DPI 切换受单显示器限制 | P2 |
| M2-09 | 40 个 P0 动作绑定表 + 启动冲突检测 + 解绑界面（STATE M2 #9） | DONE | `src/ui/ActionRegistry.cpp`（40 条、`setOverrides`、`specForKey`、`conflicts()`）；`qml/SettingsPanel.qml` 快捷键页；`m2-actions.json`（三主题各 `dispatchedCount=40`）；`settings-probe.json`（`Ctrl+F9` 落盘并重读） | — | — |
| M2-10 | 设置信息架构：通用/播放/字幕/音频/快捷键/存储/媒体库/关于 + 账户入口（STATE M2 #5 + 用户复审要求） | **DONE**（2026-11-08 补组后） | `src/settings/Preferences.*`（storage/language 键）、`qml/SettingsPanel.qml` 八页、`qml/components/OutlinedText.qml`；`settings-storage-probe.json`、`settings-behavior-probe*.json`、`window-state-*.json`、`shots/p0-settings/*`、`p0-settings-groups.json`、`settings-ten-items.json` | 三块 P0 已补并做行为级验证：存储整组、语言控件（错误文案删除）、媒体库入口（账户显式延后）；字幕描边预览与 B 第三栏绑定真实 prefs。保留 NOT_VERIFIED：更新包真实落盘、外部查看器、beta manifest 真实拉取；BT.2390 曲线降级为“无预览”。逐项见 `docs/SETTINGS-GAP.md` | — |
| M2-11 | 界面文案中文（i18n）（STATE M2 #10） | DONE | `m2-i18n.json`（中/英两套，203 条，各 37 张截图）；`src/app/main.cpp` 的 `QTranslator`/`--lang`；CI `check_i18n.py` | 语言能力本身达标；但设置页仍写“只提供中文 UI”，且无语言选择入口——这是**设置项缺口**（见 SETTINGS-GAP），不是 i18n 实现缺口 | — |
| M2-12 | 每里程碑“实机截图 × 设计稿”对照（STATE M2 收口要求） | **NOT_VERIFIED** | `UI-ALPHA5/7/8-COMPARISON.md` 存在 | **对照物（DESIGN.md / prototype）缺失**；比较文档是自述，不是与原始设计稿的逐屏对照。M0/M1/M2 也没有留完整对照记录 | P1（流程） |

## 4. M3 总表

| ID | 条目（来源） | 结论 | 证据位置 | 缺失说明 | 优先级 |
|---|---|---|---|---|---|
| M3-01 | 媒体库形态按主题分支（A 右侧 352 滑出、B 居中宽面板、C 右侧 76% 抽屉）（STATE alpha7 #1） | DONE | `qml/LibraryPanel.qml`；`shots/alpha7-matrix/m2-{A,B,C}-library.png` | 形态分支完成；真实内容由 M3-07 补齐 | — |
| M3-02 | C 默认几何 1100×680（STATE alpha7 #2） | DONE | `WindowController::start`；`ui-alpha7-window-geometry.json` | — | — |
| M3-03 | B 视频让位（`setVideoInsets`）（STATE alpha7 #3） | DONE | `WindowController::setVideoInsets`；`ui-alpha7-window-geometry.json`（B 视频右缩 320 逻辑像素） | 与 alpha10“右栏压进度条”是不同问题，后者已由 M3-10④ 修复 | — |
| M3-04 | B 技术条（codec/fps/bitrate/hwdec/HDR）（STATE alpha7 #4） | DONE | `qml/layouts/LayoutB/LayoutB.qml` techBar；`MpvClient::videoCodec/...`；`shots/alpha7-ui/main-B.png` | — | — |
| M3-05 | 设置形态：A 1020×660 双栏 / B 1080×660 三栏 / C 560×452 弹出卡（STATE alpha7 #5） | DONE | `SettingsWindow::sizeForTheme`；`shots/alpha7-matrix/*settings*` 18 张；`ui-alpha10-walkthrough.json`（设置窗 1020×660 逻辑、可拖动） | 本条只判形态；设置**内容**缺口归 M2-10 / `SETTINGS-GAP.md`，不在此重复计缺 | — |
| M3-06 | 空态规则（A/B/C 各自隐藏项）（STATE alpha7 #6） | DONE | `shots/alpha7-ui/empty-{A,B,C}.png`；各 Layout 的 `hasFile` 分支 | — | — |
| M3-07 | 媒体库真实内容源（扫描/索引/缩略图/海报墙 + 列表/表格）（STATE M4 范围，M3 遗留） | DONE | `src/media/MediaLibraryService.*`、`src/media/ThumbnailProvider.*`、`qml/LibraryPanel.qml`；`docs/evidence/ui-m4-library.json`、`ui-m4-open.json`、`shots/m4-library/*.png`、`docs/evidence/tst-media-m4.xml` | 真实样本目录索引 20 项、19 张缩略图（TS 无系统缩略图时回退占位）；三主题海报墙 + A/B 列表截图；真实点击海报后窗口标题变为 `Palune — 01-h264-1080p-aac.mp4`；单测覆盖扫描/失效条目清理/损坏索引重建/200 文件耗时 9.6s | — |
| M3-08 | 设计稿逐屏对齐（含 alpha5 §3 的 7 项偏差）（STATE alpha7） | **NOT_VERIFIED** | `UI-ALPHA5/7-COMPARISON.md` | 无 DESIGN.md/prototype 可对照；无法证明与原始设计一致 | P1（流程） |
| M3-09 | alpha8 四条：画面内方框 / 窗口边缘方框 / 最大全屏方框 / 设置被遮挡裁剪（STATE alpha8） | DONE | `STATE.md` alpha8 表；`shots/alpha8-ui/` 21 状态；`alpha8_edge_audit.py` | 当时只消了“方框”；由此引入的“暂停切走再切回残留”与设置窗不可拖动，已在 alpha10 修复并复验 | — |
| M3-10 | alpha10 六项实机缺陷（STATE alpha10） | DONE | `ui-alpha10-walkthrough.json`；`shots/alpha10-ui/01–08`；`docs/MOUSE-INTERACTION.md` | ①设置窗拖动 `moved=true` ②三主题线框预览完整 ③10 轮失焦残留 `residual_max=0.0` ④B 右栏 RGB(12,14,18) 均一 ⑤单击暂停实测 ⑥自动隐藏/悬停恢复差分 | — |
| M3-11 | 应用图标（唯一 SVG 源 + ICO/ICNS/PNG + 26 处接入）（STATE alpha9） | **PARTIAL** | `resources/brand/*`、`UI-ALPHA9-ICON.md`、`shots/alpha9-icon/` | Windows 全链路有证据；macOS 仅写入 `Info.plist`/`MACOSX_BUNDLE_ICON_FILE`，**未在真机验证显示** | P2 |

## 5. 补丁子项与主条目的对应

| 补丁 | 子项 | 归入主条目 | 结论 |
|---|---|---|---|
| alpha5 | 拖动/缩放、圆角、空闲白块 | M0-11/12/13 | DONE |
| alpha7 | 媒体库三形态、C 几何、B 让位、技术条、设置形态、空态 | M3-01–M3-06 | DONE（形态）；内容缺口归 M2-10 |
| alpha8 | 四处方框/裁剪 | M3-09 | DONE |
| alpha9 | 图标唯一源与多尺寸 | M3-11 | PARTIAL（macOS 未验） |
| alpha10 | 设置拖动、主题预览、失焦残留、B 右栏、单击语义、悬停热区 | M3-10 | DONE |

## 6. 之前报过 PASS、但实际有缺项的（明确追责）

| 曾报结论 | 位置 | 实际缺什么 | 当时是怎么误判的 |
|---|---|---|---|
| M1“判定 PASS” | README「M1 实测结论」 | 冷启动 800ms（alpha8 1466ms）、soak 2/3 轮、生产签名密钥、干净虚拟机安装（三平台包已产出但 macOS/Linux 未安装验证） | 表内单项写了“待验证/PARTIAL”，章节结论仍按能力面写成 PASS，等于用整体覆盖单项 |
| M3“判定 PASS（偏差 #7 不阻塞）” | STATE alpha7 | 媒体库无内容源；设置内容缺项未核对；无设计稿可对照 | 把“偏差 #7 不阻塞”当作 M3 可收口，未逐条核对；媒体库空态被当成形态完成 |
| “设置形态已按设计稿完成” | STATE alpha7 #5 | 只有 18 张截图里的页签与卡片；字幕/音频/播放/快捷键/账户媒体库子项当时为空或占位 | 只验证“页签存在”，没有点进每一项验证是否生效、是否落盘 |
| alpha8“四条现象根因修复 PASS” | STATE alpha8 | 修复引入/掩盖了新问题：设置窗不可拖动、激活切换吞 NC 消息导致残留 | 只验证“方框消失”，没验证交互与失焦恢复 |
| alpha9“图标接入完成 PASS” | UI-ALPHA9-ICON.md | macOS 真机未验证 | 文档自己写了“未在真机验证”，但 Windows 收口仍记为整体 PASS |
| **alpha10/S1–S6“设置各项全部补齐”** | `SETTINGS-GAP.md` 旧 §9 | 存储整组不存在；界面语言只有静态文案（且称“只提供中文”，与中英 i18n 证据矛盾）；账户/媒体库入口不在设置；字幕描边预览与 B 第三栏曲线不联动；关闭到托盘/退出确认/自动更新/代理/beta 等仅有代码，无运行时取证 | 把“六页存在 + settings-probe 覆盖的若干键值”当成“IA 全组完成”；把“显式延后”当成“有”；没有按组枚举，也没有对未取证的开关标 NOT_VERIFIED |

> 反向修正：旧复审曾把 M2-11 写成“仅中文、无多语言”，但 `m2-i18n.json` 与 `src/main.cpp` 证明中英双语已实现。这是**低估**，不是缺口；相关缺口在设置页缺少语言入口与错误文案。

## 7. 结论

### 7.1 必须补的硬缺口

0. **版本口径**：`1.0.0` 本地包只是**功能冻结候选 RC**（alpha 段已到上限后的版本号），不是缺口闭合的正式版。设置 P0 已于 11-08 闭合（M2-10 转 DONE），但 P1（生产签名/更新通道、计划文档缺失）未清，仍禁止对外宣称“1.0.0 已收口”。
1. **[已补 11-08] 设置信息架构缺组**：新增「存储」「媒体库」两页；语言控件与错误文案修正；账户入口写明本地模式并显式延后。证据见 `SETTINGS-GAP.md` §12。
2. **[已补 11-08] 设置预览类“有控件无效果”**：字幕描边预览改 `OutlinedText` 8 向偏移并绑定 `sub.outline`；B 第三栏绑定真实 prefs；BT.2390 静态 Canvas 已删除并明确标注不可用（NOT DONE，非占位）。
3. **[已补 11-08] 设置运行时取证**：十项已在真实进程取证，8 项 DONE；下载真实落盘/外部查看器/beta manifest 拉取标 NOT_VERIFIED。证据 `docs/evidence/settings-ten-items.json`。
4. **[已补] 媒体库运行取证（M3-07）**：11-08 16:20 补齐 `shots/m4-library/`（三主题海报墙、A/B 列表、点击开播）与 `ui-m4-library.json`（20 项/19 缩略图）、`tst-media-m4.xml`（8/8，含失效清理、损坏索引重建、200 文件 9.6s），该项转 DONE。
5. **[P1] 生产签名密钥与真实更新通道（M1-18）**：当前是 `palune-test` 仓库内密钥；stable 发版前必须换成离线生产密钥，并验证回退通道。
6. **[P1 部分闭合 11-08] 计划文档缺失（M2-12 / M3-08 的根因）**：`ARCHITECTURE.md` 已纳入本仓 `docs/ARCHITECTURE.md`（文件头写明归属与维护规则），架构/标识类验收可权威复核；`MILESTONES.md`、`DESIGN.md` 仍不在工作区，逐条里程碑验收与逐屏对齐仍缺依据。
7. **[P2] 平台与分发**：macOS/Linux 真机运行（M0-10）；干净虚拟机安装、macOS/Linux 包实际安装、MSI 安装流程（M1-19，三平台安装包本身已产出）；macOS 图标显示（M3-11）。
8. **[P3] 性能与体验**：冷启动 1466/2116ms 对 800ms 基线（M1-11）、soak 只跑 1/3 轮（M1-17）、跨屏 DPI 与真实 Snap 手势（M0-07/09、M2-08）。

### 7.2 可显式延后（附理由）

| 项 | 理由 | 延后到 |
|---|---|---|
| macOS/Linux 真机（M0-10 / M1-16 补充取证 / M1-19） | 本机无设备；CI 只证明构建与冒烟，安装包已产出但未实际安装 | 1.0.0 前由真机补齐 |
| 干净虚拟机 / MSI 安装流程（M1-19） | MSI 已由 CI 产出，但没有干净环境安装记录 | 发布机补齐 |
| 生产签名密钥（M1-18） | 只能由用户签发离线密钥，不能自造 | stable 前硬门槛 |
| soak 第 2/3 轮（M1-17） | 每轮 60 分钟，属长耗时非功能阻塞 | 1.0.0 前 |
| 跨屏 DPI / 真实拖拽贴边（M0-07/09、M2-08） | 单显示器环境，合成消息不算证据 | 第二显示器可用时 |
| 设计稿逐屏对照（M3-08 / M2-12） | 原始 DESIGN.md/prototype 缺失，无法补造 | 用户提供设计稿后重做 |
| 音频增益/夜间模式/输出后端、字幕字体回退、截图/录制、缩放算法、手动代理地址 | 不在现有验收项；补了会扩大范围 | 1.1 之后按需求再评估 |
| 账户云端/媒体库服务端适配器 | 当前只有本地库；`MediaServerAdapter` 注册表为空 | M5 内容源扩展时 |

### 7.3 建议执行顺序

| 批次 | 内容 | 对应优先级 |
|---|---|---|
| 1 | 设置 IA 整组补齐：存储组、界面语言控件与文案、账户/媒体库入口；同时修字幕描边预览与 B 第三栏静态占位 | P0 |
| 2 | 设置运行时取证：托盘/退出确认/启动最大化/音频设备/更新通道/代理/下载目录/许可，逐项补 probe 或截图 | P0 |
| 3 | 媒体库运行取证（扫描、索引、缩略图、海报墙/表格、损坏索引重建） | P1 |
| 4 | 恢复 MILESTONES/DESIGN/ARCHITECTURE，重做 M2-12/M3-08 对照 | P1 |
| 5 | 生产签名密钥、真实更新通道与回退验证 | P1 |
| 6 | macOS/Linux 真机、安装包、MSI、干净虚拟机 | P2 |
| 7 | 冷启动回归到 800ms、soak 补 2 轮、跨屏 DPI/真实贴边 | P3 |

### 7.4 本次复审新增/修正的判定（相对旧版 GAP-AUDIT）

| 条目 | 旧判定 | 本版判定 | 依据 |
|---|---|---|---|
| M0-07 | NOT_VERIFIED | PARTIAL | `m2-scale-matrix.json` 已覆盖 125/150/200%，alpha10 矩阵有 100/150% 实拍；仍无真实多屏 |
| M1-06 | PARTIAL | DONE（抽象层） | 五组接口 + 注册表齐全；缺的是 M4 本地源，已由 M3-07 落地 |
| M1-15 | NOT_VERIFIED | DONE | CI 覆盖率作业带 `--fail-under-line 70` 硬门槛且 STATE 记录绿色；仅缺归档 |
| M1-16 | NOT_VERIFIED | DONE | CI 三平台冒烟作业 + STATE“macOS 20/20”；仅缺归档 |
| M2-10 | PARTIAL | DONE（11-08 补组） | 存储/语言/账户媒体库三组已补并做行为取证；剩余 NOT_VERIFIED 单列 |
| M2-11 | DONE（“仅中文”） | DONE（中英 i18n） | `m2-i18n.json` 203 条、37 张 ×2；旧描述低估 |
| M3-01 | DONE（无真实数据） | DONE | 真实数据由 M3-07 代码补齐 |
| M3-05 | PARTIAL | DONE（形态） | 形态/尺寸/拖动均达标；内容缺口归 M2-10，不重复计 |
| M3-07 | NOT DONE | DONE | 运行证据与单测已补齐（`ui-m4-library.json`、`shots/m4-library/`、`tst-media-m4.xml`） |
| M3-10 | DONE | DONE | 证据齐全；旧 §6.1 仍把它列为硬缺口，属文档未同步，已删除 |
| M3-11 | DONE | **PARTIAL** | Windows 有证据；macOS 仅配置，未真机验证 |
| M1-11 | PARTIAL（1466ms） | PARTIAL（2116ms） | 追加 STATE alpha10 记录，回归扩大 |

## 8. 关联文档

- 设置逐项清单：`docs/SETTINGS-GAP.md`
- 里程碑历史：`docs/STATE.md`
- 鼠标语义：`docs/MOUSE-INTERACTION.md`
- 更新链：`docs/evidence/UPDATE-CHAIN-ALPHA6.md`
