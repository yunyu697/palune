# 里程碑复审（M0–M3）GAP-AUDIT

- 审计日期：2026-11-08（Asia/Shanghai）
- 范围：M0、M1、M2、M3（含 alpha4–alpha9 期间的补丁项）
- 判定口径：**DONE**（有实现 + 可定位证据）/ **PARTIAL**（做到一部分，缺项写清）/ **NOT DONE**（没做）/ **NOT_VERIFIED**（做了但无法验证，写原因）
- 纪律：禁止"整体 PASS"覆盖单项缺失；缺失不写成 PARTIAL；此前判过 PASS 但实际缺项的，单列追责。

## 0. 审计前提（必须先看）

| 事实 | 说明 | 影响 |
|---|---|---|
| `MILESTONES.md`、`DESIGN.md`、`prototype-*.html` 在仓库与 git 历史中**都不存在** | `git log --all -- MILESTONES.md DESIGN.md` 为空；全盘只有 README / NOTICE / docs/STATE.md / docs/RELEASES.md / docs/evidence/* | 本复审的"验收条目"只能从 `docs/STATE.md`（24KB，按 alpha 分节的历史验收与修复）、`README.md`（M0/M1/M2 已完成清单）与 `docs/evidence/*` 反推。**这是复审本身的第一个缺口，不是条目缺失** |
| 因此"设计稿逐屏对照"没有权威对照物 | UI-ALPHA5/7/8-COMPARISON.md 是自述对照，无法证明与原始设计稿一致 | 所有"与设计稿一致"的判断只能记为 **NOT_VERIFIED**，不能记为 DONE |
| 本机环境 | Windows 10 桌面会话、单显示器 3840×2160@200%；无 WiX；无 macOS/Linux 机器 | 三平台、跨屏 DPI、MSI 等只能 NOT_VERIFIED |

> 本文件不补造任何"原文档条款"。条目均标注来源（STATE.md 章节 / README 段落 / 证据文件名）。

## 1. M0

| ID | 条目（来源） | 结论 | 证据位置 | 缺失说明 | 优先级 |
|---|---|---|---|---|---|
| M0-01 | 双窗架构：VideoWindow + OverlayWindow（README「已完成 M0」） | DONE | `src/window/VideoWindow.cpp`、`OverlayWindow.cpp`、`WindowController.cpp`；`docs/evidence/m0-report.json`（`appWindowCount=1`） | — | — |
| M0-02 | libmpv 运行时加载，`vo=gpu-next` + `gpu-context=d3d11` + `hwdec=d3d11va` | DONE | `src/mpv/MpvClient.cpp`；`docs/evidence/m1-smoke.json` | — | — |
| M0-03 | 最小能力：打开文件/播放暂停/拖动进度/音量/全屏/退出 | DONE | `src/app/AppController.cpp`、`WindowController::action`；`m1-smoke.json` 20/20 | — | — |
| M0-04 | 设置页更新模块：检查更新/打开主页常驻、镜像切换、SHA-256 + Ed25519 | DONE | `qml/UpdateSection.qml`、`src/update/*`；`report-e2e-full.json`、`report-check-badsig.json`、`report-mirror-poison.json`、`report-mirror-404.json` | 签名密钥仍是仓库内测试私钥（见 M1-18） | — |
| M0-05 | 版本号校验接入 CI + 三平台构建 | DONE | `tools/version/check_version.py`；README CI 链接 runs/42216916696 | — | — |
| M0-06 | 全屏切换 20 次：几何偏移 ≤1 帧、不黑 | DONE | `docs/evidence/m0-report.json`（最大同步时差 6.284ms、亮度 20/20） | — | — |
| M0-07 | DPI 100/150/200 各拖动 10 次、跨屏零偏移 | **NOT_VERIFIED** | `m0-report.json` 自述"仅 1 屏、固定 100%"；`m2-window.json` 运行中 DPI 亦标未验证 | 无第二显示器/真实跨屏拖动；`WM_DPICHANGED` 合成消息不作为达标证据 | P3 |
| M0-08 | Alt-Tab 只有 1 个应用窗口、恢复画面正常 | DONE | `m0-report.json`（窗口数 1、最小化/恢复 10 轮亮度非黑） | 仅验证"恢复后画面非黑"，**未验证"暂停态切走再切回无残留"**（该问题在 alpha10 暴露，见 M3-10） | — |
| M0-09 | Snap 四边贴边 + 最小化恢复零偏移 | PARTIAL | `m0-report.json` | 用 `SetWindowPos` 模拟系统贴边，**非鼠标拖拽手势**；真实拖拽贴边未验证 | P3 |
| M0-10 | macOS/Linux 渲染路径 | **NOT_VERIFIED** | `src/mpv/MpvClient.cpp`（`#if defined(Q_OS_MACOS)` 分支） | 仅保留接口与配置分支，从未在真机运行 | P2 |
| M0-11 | 窗口拖动 / 四边四角缩放（alpha5 修复） | DONE | `docs/evidence/window-drag-resize.json`（drag 200×100、resize +300） | 当时为程序化注入，非真实指针 | — |
| M0-12 | 无边框窗口圆角 | DONE | `docs/evidence/window-radius.json`（A/B/C = 27/20/41 物理） | — | — |
| M0-13 | 空闲视频子窗不露白块 | DONE | `window-radius.json` / alpha5 截图矩阵 | — | — |
| M0-14 | 明确不做：位图皮肤、3D/VR、软解滤镜矩阵、8K 承诺 | 排除 | README「明确不做」 | 不计入缺口 | — |

## 2. M1

| ID | 条目（来源） | 结论 | 证据位置 | 缺失说明 | 优先级 |
|---|---|---|---|---|---|
| M1-01 | 四层主题：tokens → components → layouts → ThemeRegistry/LayoutHost | DONE | `qml/tokens`、`qml/components`、`qml/layouts/*`、`src/theme/*` | — | — |
| M1-02 | VideoHost 在布局树外，切主题不触碰视频承载窗 | DONE | `LayoutHost::layoutContainsVideoHost()`；`m1-theme-switch.json`（`start-file`/`end-file` 增量 0） | — | — |
| M1-03 | 主题持久化 + 缺键/非法/资源缺失回退 A | DONE | `src/theme/ThemeRegistry.cpp`；`m2-config-fallback.json`（5 场景）；`tst_theme` | — | — |
| M1-04 | 最小 UI：打开/拖拽/播放暂停/seek/音量/速度/音字幕轨/全屏/退出/最近/三布局 | DONE | README M1 清单；`qml/Main.qml`、`qml/layouts/*` | — | — |
| M1-05 | settings.toml 原子读写、保留未知键、窗口几何恢复 | DONE | `src/settings/Settings.cpp`（`QSaveFile`）、`Toml.cpp`；`tests/unit` | — | — |
| M1-06 | 内容源抽象：五组接口 + MediaItem/UserState + 适配器注册表 | PARTIAL | `src/media/MediaLibraryService.h`、`MediaServerAdapter.h`、`Types.h` | 只有接口与空库状态；**无任何真实内容源**（M4 项） | P0（M4） |
| M1-07 | 分级日志 / 崩溃面包屑 / 版本信息页 | DONE | `src/diag/*`、`qml/SettingsPanel.qml` 关于页 | — | — |
| M1-08 | 更新清单 payload + 分离签名信封 | DONE | `src/update/UpdateManifest.*`；`docs/evidence/UPDATE-CHAIN-ALPHA6.md` | — | — |
| M1-09 | 命名与标识冻结（appId、`%APPDATA%\Palune`、日志前缀、`palune://`） | DONE | `src/core/AppPaths.*`、`CMakeLists.txt` | — | — |
| M1-10 | 20 样本 × 5s 冒烟（Windows） | DONE | `docs/evidence/m1-smoke.json` 20/20 | — | — |
| M1-11 | 冷启动 ≤800ms | **PARTIAL** | `m1-perf.json` 574ms（进程启动→主窗可交互）；`docs/evidence/install-launch-alpha8.json` windowReadyMs **1466** | 历史基线达标；alpha8 安装包实测 1466ms，**已过 800ms 基线**，且未在 GAP 复审前被指出 | P2 |
| M1-12 | 空闲内存 ≤300MB | DONE | `m1-perf.json` 94.1MB | — | — |
| M1-13 | 1080p 播放内存 ≤500MB，丢帧/错时帧 0 | DONE | `m1-perf.json` 204.5MB | — | — |
| M1-14 | 主题切换 10 次 ≤300ms、不断播、无黑帧 | DONE | `m1-theme-switch.json` 最大 104.1ms | — | — |
| M1-15 | 覆盖率 ≥70% | **NOT_VERIFIED** | README 明写"待验证"；无覆盖率报告文件 | 本地与 CI 都未产出覆盖率数值 | P2 |
| M1-16 | 20 样本 × 3 OS 冒烟（macOS/Linux 各 ≥19/20） | **NOT_VERIFIED** | README 明写"本机仅有 Windows；CI headless 冒烟结果为准"；仓库无 CI 冒烟结果文件 | 无 CI 结果下载/归档；无第二平台真机 | P2 |
| M1-17 | 1h × 3 循环 RSS 无持续增长 | PARTIAL | `docs/evidence/m1-soak.json` | 只完成 1 轮 60 分钟（-28.3MB/h）；缺 2 轮 | P3 |
| M1-18 | 签名与更新通道：可签名、可回退 | PARTIAL | `UPDATE-CHAIN-ALPHA6.md`；`resources/trust/update.pub` | 使用仓库内**测试**私钥（`palune-test`）；无生产离线密钥/域名 | P1（发版前硬门槛） |
| M1-19 | 三平台安装包 + 干净虚拟机 | PARTIAL | `windows-install-evidence.json`（Windows 真实装/启/卸） | 仅在**本机**验证，非干净虚拟机；macOS/Linux 安装包未验证；MSI 本机无 WiX 未产出 | P2 |
| M1-20 | 单元测试 | DONE | `ctest -C Release` 7/7（STATE alpha9） | 覆盖深度未与覆盖率联动（见 M1-15） | — |
| M1-21 | CI 分层静态检查 + 三平台构建 | DONE | README CI；`check_version.py`、`check_brand_terms.py` | — | — |

## 3. M2

| ID | 条目（来源） | 结论 | 证据位置 | 缺失说明 | 优先级 |
|---|---|---|---|---|---|
| M2-01 | 三主题 × 12 界面 = 36 张 2880×1800 基线 | DONE | `docs/evidence/m2-ui-baseline.json`、`docs/evidence/shots/m2-baseline/` | — | — |
| M2-02 | 缩放/分辨率矩阵 6 组（480×270 → 4K@200%） | DONE | `m2-scale-matrix.json`、`shots/m2-scale/` | — | — |
| M2-03 | 窄窗口设置面板（横向页签 + 可滚动） | DONE | `qml/SettingsPanel.qml` `narrow` 分支；`m2-scale-matrix.json` | — | — |
| M2-04 | 布局 B 左坞避让 / 低高度压缩 | DONE | `qml/layouts/LayoutB/LayoutB.qml`；`m2-scale-matrix.json` | — | — |
| M2-05 | 50 次主题切换状态保持 | DONE | `m2-stateswitch.json` | — | — |
| M2-06 | 紧凑模式：断点 560、可配、±24 滞回、60ms 防抖、10Hz 抖动 0 切换 | DONE | `src/ui/CompactController.cpp`；`m2-compact.json`、`m2-compact-baseline.json` | — | — |
| M2-07 | 配置回退矩阵 5 场景 | DONE | `m2-config-fallback.json` | — | — |
| M2-08 | 窗口回归 20 项 | PARTIAL | `docs/evidence/m2-window.json`（20 项，跨屏移动与运行中 DPI 2 项 NOT_VERIFIED） | 与 M0-07 同一硬件限制 | P3 |
| M2-09 | 40 个 P0 动作绑定表 + 启动冲突检测 + 解绑界面 | PARTIAL | `src/ui/ActionRegistry.cpp`（40 条，平台键位 + `conflicts()`）；`qml/SettingsPanel.qml` 快捷键页仍写"未完成" | **无解绑/改键界面**；40 条动作未逐条实机验证；设置页文案与实现不同步 | P1 |
| M2-10 | 设置信息架构（通用/播放/字幕/音频/快捷键/关于 + 存储/更新/账户媒体库入口） | **PARTIAL → 见 `docs/SETTINGS-GAP.md`** | `qml/SettingsPanel.qml`、`src/settings/Settings.*` | 多个分组只有只读展示或占位文案；字幕字号/延迟/外挂、音频输出、硬解/缓冲/HDR、快捷键改键、账户/媒体库入口均缺 | P0 |
| M2-11 | 界面文案中文（i18n） | DONE | `m2-i18n.json`；QML 全量 `qsTr` | 仅中文；无语言切换（需求未要求多语言） | — |
| M2-12 | 每里程碑"实机截图 × 设计稿"对照 | **NOT_VERIFIED** | `UI-ALPHA5/7/8-COMPARISON.md` 存在 | **对照物（DESIGN.md / prototype）缺失**，无法证明一致性；且 M0/M1/M2 收口时没有留下该对照 | P0（流程） |

## 4. M3

| ID | 条目（来源） | 结论 | 证据位置 | 缺失说明 | 优先级 |
|---|---|---|---|---|---|
| M3-01 | 媒体库形态按主题分支（A 右侧 352 滑出、B 居中宽面板、C 右侧 76% 抽屉） | DONE | `qml/LibraryPanel.qml`；`shots/alpha7-matrix/m2-{A,C}-library.png` | 只有空态，无真实数据（M3-07） | P0（M4） |
| M3-02 | C 默认几何 1100×680 | DONE | `WindowController::start`；`ui-alpha7-window-geometry.json` | — | — |
| M3-03 | B 视频让位（`setVideoInsets`） | DONE | `WindowController::setVideoInsets`；`ui-alpha7-window-geometry.json`（B 视频右缩 320 逻辑） | 与 alpha10 的"右栏压进度条"是不同问题（后者为 chrome 切换 + 几何余量） | — |
| M3-04 | B 技术条（codec/fps/bitrate/hwdec/HDR） | DONE | `LayoutB.qml` techBar；`MpvClient::videoCodec/...`；`shots/alpha7-ui/main-B.png` | — | — |
| M3-05 | 设置形态 A 1020×660 双栏 / B 1080×660 三栏 / C 560×452 弹出卡 | PARTIAL | `SettingsWindow::sizeForTheme`；`shots/alpha7-matrix/*settings*`（18 张） | 形态与尺寸在、拖动已修（alpha10）；**内容仍大量缺失**（SETTINGS-GAP.md，批 2 整组补齐） | P0 |
| M3-06 | 空态规则（A/B/C 各自隐藏项） | DONE | `shots/alpha7-ui/empty-{A,B,C}.png`；各 Layout 的 `hasFile` 分支 | — | — |
| M3-07 | 媒体库真实内容源（扫描/索引/缩略图/海报墙 + 列表/表格） | **NOT DONE** | `MediaLibraryService` 为空实现（类注释"M1 为空实现"）；`qml/library/MediaLibraryEmpty.qml` | 完全没有本地目录扫描、索引、缩略图、海报墙/列表表格 | P0（M4） |
| M3-08 | 设计稿逐屏对齐（含 alpha5 §3 的 7 项偏差） | **NOT_VERIFIED** | `UI-ALPHA5/7-COMPARISON.md` | 无 DESIGN.md/prototype 可对照；对照文档是自述标准 | P1（流程） |
| M3-09 | alpha8 四条：画面内方框 / 窗口边缘方框 / 最大全屏方框 / 设置被遮挡裁剪 | DONE | `STATE.md` alpha8 表；`shots/alpha8-ui/` 21 状态；`alpha8_edge_audit.py` | 吞 `WM_NCACTIVATE/WM_NCPAINT` 只影响 NC 绘制；alpha10 复验证明"暂停切走再切回残留"的真实根因是视频窗与覆盖窗同级顶层窗的 **z 序**，不是 NC 处理或 DWM 缓冲（见 M3-10③） | — |
| M3-10 | alpha10 六项实机缺陷 | DONE | `docs/evidence/ui-alpha10-walkthrough.json`；`shots/alpha10-ui/01–08`；根因表见 `STATE.md` alpha10 | ①设置窗拖动 `moved=true` ②三套主题线框预览完整 ③10 轮失焦残留 `residual_max=0.0` ④B 右栏 RGB(12,14,18) 均一且与底栏分离 ⑤`docs/MOUSE-INTERACTION.md` 定义并实测单击暂停 ⑥自动隐藏可见 03 图、悬停恢复差分 68.7% | — |
| M3-11 | 应用图标（唯一 SVG 源 + ICO/ICNS/PNG + 26 处接入） | DONE | `resources/brand/*`、`docs/evidence/UI-ALPHA9-ICON.md`、`shots/alpha9-icon/` | macOS 侧仅写入 Info.plist/`MACOSX_BUNDLE_ICON_FILE`，未在真机验证 | P2（macOS） |

## 5. 之前报过 PASS、但实际有缺项的（明确追责）

| 曾报结论 | 位置 | 实际缺什么 | 当时是怎么判的 |
|---|---|---|---|
| M1 "判定 PASS" | README「M1 实测结论」表 | 覆盖率 ≥70%（NOT_VERIFIED）、三平台冒烟（NOT_VERIFIED）、soak 2/3 轮、签名生产密钥、三平台安装 | 表格里单项写了"待验证/PARTIAL"，但章节结论仍按能力面收口，等于用整体覆盖单项 |
| M3 "判定 PASS（偏差 #7 NOT_VERIFIED，不阻塞）" | STATE.md alpha7 | 媒体库无内容源；设置内容缺项未核对；无设计稿可对照 | 把"偏差 #7 不阻塞"当作 M3 可收口，且未按条目逐项核对 |
| "设置形态已按设计稿完成" | STATE.md alpha7 #5 | 只有形态与页签；字幕/音频/播放/快捷键/账户媒体库等子项为空或占位 | 只看了 18 张截图里"页签存在"，没点进每一项验证功能性 |
| alpha8 "四条现象根因修复 PASS" | STATE.md alpha8 | 修复引入/掩盖了新问题：设置窗不可拖动、激活切换吞消息导致残留 | 只验证了"方框消失"，没验证交互与失焦恢复 |
| alpha9 "图标接入完成 PASS" | UI-ALPHA9-ICON.md | macOS 真机未验证 | 明确写了"未在真机验证"，但不影响 Windows 收口 |

## 6. 结论

### 6.1 必须补的硬缺口（用户可直接感知）

1. **设置窗口不可拖动**（M3-10①）—— 独立顶层窗没有任何 `startSystemMove` 命中区，标题栏区域被 `SettingsPanel` 卡片内容覆盖。
2. **暂停态失焦再回焦点画面残留**（M3-10③）—— alpha8 为消方框在 `OverlayWindow` 子类里对 `WM_NCACTIVATE/WM_NCPAINT` 直接返回 TRUE、且不触发重绘，激活切换后窗口缓冲没有失效，DWM 保留上一窗口遮挡时的像素。
3. **布局 B 点击后右栏消失/变透明、右栏压进度条**（M3-10④）—— 点击落在 `Main.qml` 全窗 `MouseArea` 上切换 `chromeVisible`，右栏随之隐藏；且控制条横跨全宽，右栏底边余量依赖 `techBarHeight/controlBarHeight` 常量，状态变化时会重叠。
4. **鼠标操作无统一定义、控制栏无悬停热区**（M3-10⑤⑥）—— 目前单击=切换控件显示、双击=全屏、滚轮无绑定、无自动隐藏、无悬停显示。
5. **设置内容大面积为空/占位**（M2-10）—— 见 `docs/SETTINGS-GAP.md`，按组补齐。
6. **媒体库无真实内容**（M3-07/M1-06）—— M4 范围，紧随 alpha10。

### 6.2 可显式延后（需写理由）

| 项 | 理由 | 延后到 |
|---|---|---|
| macOS/Linux 真机（M0-10 / M1-16 / M1-19） | 本机无设备，CI 只证明可构建 | 1.0.0 前由 CI+真机补齐 |
| 覆盖率 ≥70%（M1-15） | 需要 CI 出数，非功能阻塞 | 1.0.0 前 |
| 跨屏 DPI / 真实贴边手势（M0-07 / M0-09 / M2-08） | 单显示器环境，合成消息不算证据 | 第二显示器可用时 |
| MSI（M1-19） | 本机无 WiX；NSIS 已覆盖安装路径 | CI 补齐 |
| 生产签名密钥（M1-18） | 需用户签发离线密钥，不能自造 | 发 stable 前（硬门槛） |
| soak 2 轮（M1-17） | 长耗时，不阻塞功能 | 1.0.0 前 |
| 设计稿对照（M3-08 / M2-12） | 原始 DESIGN.md/prototype 不在仓库，无法补造 | 由用户提供设计稿后重做；在此之前所有"对齐设计稿"只写 NOT_VERIFIED |

### 6.3 执行批次（按优先级）

| 批次 | 内容 | 对应用户优先级 |
|---|---|---|
| 批 1 | 设置窗拖动、失焦残留、B 右栏、鼠标定义与热区（alpha10 六项） | 用户直接感知错误 |
| 批 2 | 设置分组整组补齐（字幕、音频、播放、快捷键、通用持久化） | 用户直接感知错误 |
| 批 3 | 配置持久化与数据完整性核对（所有设置项落 `settings.toml`、重启保持） | 安全与数据完整性 |
| 批 4 | 启动时间回归（1466 → 800ms 以内）、MSI/三平台/覆盖率 | 平台兼容与分发 |
| 批 5 | M4 媒体库真实内容源 | 功能里程碑 |
| 批 6 | soak、跨屏 DPI、真机贴边、设计稿复核 | 性能与体验细节 |

## 7. 关联文档

- 设置完整性逐项清单：`docs/SETTINGS-GAP.md`
- 里程碑收口标准：`docs/STATE.md`「里程碑收口标准」一节
- 鼠标操作定义：`docs/MOUSE-INTERACTION.md`（alpha10 新增）
