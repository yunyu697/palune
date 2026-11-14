# Palune

轻量、无位图皮肤、面向 Windows 优先的桌面视频播放器。

当前版本：**1.0.0-RC1**（`VERSION` = `1.0.0-RC1`；功能冻结候选，**尚未收口**，见「已知限制」；对外不得称正式版）。

已实现主线（截至 2026-11-13）：

| 能力 | 状态 | 入口 |
| --- | --- | --- |
| 三主题 A/B/C + 四层主题架构 | 已实现 | `qml/layouts/*`、`src/theme/*` |
| 本地媒体库（扫描 / 索引 / 缩略图 / 海报墙 + 列表） | 已实现 | `src/media/MediaLibraryService.*`、`qml/LibraryPanel.qml` |
| 媒体服务器 | **Jellyfin / Emby 已可用**（连接表单、测试连接、保存/删除、重启回填、拉库与进度上报）；极空间、飞牛选「其他 Jellyfin」；**Plex 未完成**（仅骨架，未验证，设置页暂无入口） | 设置 → 媒体库 → 媒体服务器；`src/media/JellyfinAdapter.*`、`PlexAdapter.*` |
| 凭据存储 | token/密码走系统凭据存储（Windows Credential Manager；macOS Keychain / Linux libsecret 同接口），旧明文自动迁移并删除 | `src/core/CredentialStore.*` |
| 更新机制（清单签名 + 产物签名 + 镜像回退 + 断点续传） | 已实现（测试密钥） | `src/update/*` |
| 设置（8 页：通用 / 播放 / 字幕 / 音频 / 快捷键 / 媒体库〔含媒体服务器〕/ 存储 / 关于） | 已实现，独立顶层设置窗 | `qml/SettingsPanel.qml`、`src/settings/Preferences.*` |
| 自绘窗口（无边框主窗 + 独立设置窗，单一任务栏/Alt-Tab 入口） | 已实现 | `src/window/*` |
| Windows 安装包自检 | 安装器落盘自检 + 打包前置门禁（缺 `platforms/qwindows.dll` 等直接失败） | `CMakeLists.txt`、`tools/release/*` |

## 已完成（M1）

- 四层主题：`qml/tokens`（L0 令牌）→ `qml/components`（L1 组件，只发信号）→ `qml/layouts/LayoutA|B|C`（L2 布局，只读播放器属性并调用 `win.action`）→ `ThemeRegistry` + `LayoutHost`（L3，只重建布局子树）
- `VideoHost` 位于布局树外，切换布局不触碰视频承载窗口；主题持久化到 `settings.toml` 的 `[ui] theme`，缺键/非法值/资源缺失均回退 A
- 最小 UI：打开文件/文件夹、拖拽、播放暂停、seek、音量、速度、音轨/字幕轨、全屏、退出、最近文件、三套布局
- `settings.toml` 原子读写（保留未知键）、窗口几何恢复、`UiStateStore` 骨架
- 内容源抽象：五组接口（认证/列表/取流/上报/能力）+ `MediaItem`/`UserState` + 适配器注册表，媒体库空状态页
- 分级日志、崩溃面包屑（`crashes/breadcrumbs.txt` 与致命信号快照）、版本信息页
- 更新清单升级为 payload + 分离签名信封，按 platform/arch 选择产物并校验产物签名
- 命名与标识冻结：`app.palune.player`、`%APPDATA%\Palune`、日志前缀 `palune`、`Palune/crashes`、`palune://`
- 更新源为本仓（源码仓已公开）的 Release 清单，支持多源回退：直连 `https://github.com/yunyu697/palune/releases/latest/download/latest.json` → 内置实测加速 `https://ghfast.top/` → 可选 `PALUNE_UPDATE_MANIFEST_MIRRORS`；匿名可读，客户端不内嵌凭据
- CI：版本校验 → 分层静态检查 → 三平台构建 + 全部单测 + 20 样本 headless 冒烟

## M1 实测结论（本机 Windows 11，独立桌面运行）

报告：`docs/evidence/m1-smoke.json`、`docs/evidence/m1-perf.json`、`docs/evidence/m1-theme-switch.json`

| 项目 | 判据 | 结果 | 实测 |
| --- | --- | --- | --- |
| 20 样本 × 5s 冒烟（Windows） | ≥19/20 通过 | **PASS** | 20/20 通过，覆盖 MP4/MKV/WEBM/TS/AVI/MOV 与 H.264/HEVC/VP9/VP8/MPEG-2/MPEG-4/MJPEG |
| 冷启动 | ≤800ms | **PASS** | 574ms（进程启动→主窗口可交互）；首个 1080p 首帧 1433ms（单独记录，不计入冷启动） |
| 空闲内存 | ≤300MB | **PASS** | 94.1MB |
| 1080p 播放内存 | ≤500MB | **PASS** | 204.5MB（播放 15s 后），丢帧 0、错时帧 0 |
| 主题切换 10 次（1080p） | 每次 ≤300ms、不断播、无黑帧 | **PASS** | 最大 104.1ms；布局构建 ≤6.7ms；亮度采样 125.7±1 全部非黑；`start-file`/`end-file` 增量均为 0；布局内无 `VideoHost` |
| 主题回退与切换单测 | 缺键/非法/资源缺失回退 A；切换 <300ms | **PASS** | `tst_theme` 覆盖 7 组场景 |
| 覆盖率 | ≥70% | **PASS（CI 门槛）** | `ci.yml` 覆盖率作业带 `gcovr --fail-under-line 70` 硬门槛且为绿；原始覆盖率报告未归档，离线复核需重跑 CI |
| 20 样本 × 3 OS 冒烟 | macOS/Linux 各 ≥19/20 | **PASS（CI）** | `ci.yml` 三平台矩阵各跑 `--m1-smoke` 并上传 artifact（macOS 20/20）；原始 JSON 未下载归档 |
| 1h × 3 循环 RSS 漂移 | 无持续增长 | **PARTIAL** | 已完成 1 轮 60 分钟：RSS 218MB→190MB，漂移 -28.3MB/h（无增长）；仍缺 2 轮 |
| 签名与更新通道 | 可签名、可回退 | **PARTIAL** | 清单/产物签名与校验链路具备且单测通过；无正式发布私钥、`updates.palune.app` 域名不受控 |
| 安装包 ×3 OS + 干净虚拟机 | 三平台可装 | **PARTIAL** | Windows 已完成真实安装→启动→卸载（见下）；macOS/Linux 未验证 |

## 已完成（M2）

- 三主题界面评审基线：A/B/C × 12 个界面 = 36 张 2880×1800 截图（mini 960×540），清单 `docs/evidence/m2-ui-baseline.json`，代表图 `docs/evidence/shots/m2-baseline/`
- 缩放/分辨率矩阵（6 组）：480×270、1280×720、4K（100%）与 125%/150%/200%（1440×900），每组 36 张全部 PASS；同时修掉两处尺寸回归——窄窗口设置面板（横向页签 + 可滚动内容）与布局 B 左坞（避让品牌栏、低高度压缩内容），见 `docs/evidence/m2-scale-matrix.json` 与代表图 `docs/evidence/shots/m2-scale/`
- 50 次主题切换状态保持：滚动位置、选中项、面板宽度、播放位置、焦点全部保持；无黑帧 >100ms、无重新加载、`position` 不倒退
- 紧凑模式自动降级：默认断点 560px、`settings.toml` 可配（非法值回退并提示）、±24px 滞回、60ms 防抖；10Hz 抖动 50 次切换数为 0
- 配置回退矩阵（5 场景）：无 settings / 无 `[ui]` / 非法主题 Z / TOML 损坏 / 合法 B，全部回退 A 并给出原因
- 报告：`docs/evidence/m2-stateswitch.json`、`m2-compact.json`、`m2-config-fallback.json`、`m2-ui-baseline.json`

## 已完成（M3 界面收口 / M4 本地媒体库）

- M3：媒体库按主题分支（A 右侧 352 滑出栏、B 居中宽面板、C 右侧 76% 抽屉）；设置改为独立顶层窗（可拖动、贴右下 16px）；无边框窗去 NC 合成层（画面/边缘无浅色框）；布局 B 右栏不透明且不压控制条；鼠标语义统一（单击播放暂停、双击全屏、右键菜单、滚轮音量、Ctrl+滚轮 seek）；控制栏声明式自动隐藏
- M4：递归扫描本地目录（格式过滤、跳过不可读/符号链接）、索引持久化与损坏重建、系统缩略图（`IShellItemImageFactory`，按 mtime 命名）、海报墙与列表两形态、点击条目直接开播、失效根目录/文件清理、扫描在工作线程不阻塞 UI
- 设置补齐（2026-11-08）：存储页（占用统计、按上限淘汰缩略图、索引重建、配置导出/导入、日志保留）、语言入口（`ui.language` = system/zh_CN/en_US，重启生效）、媒体库入口页、字幕描边实时预览与布局 B 第三栏绑定
- 证据：`docs/GAP-AUDIT.md`、`docs/SETTINGS-GAP.md`、`docs/STATE.md`、`docs/evidence/`（`ui-alpha10-*.json`、`settings-storage-probe.json`、`settings-behavior-probe*.json`、`shots/p0-settings/`）

## Windows 安装器修复（alpha4）

| 用户问题 | 根因 | 修复与证据 |
| --- | --- | --- |
| 装完程序无法运行 | 安装树缺 MSVC 运行时（`vcruntime140.dll` 等）与 QML 模块（`windeployqt` 缺 `--qmldir`） | 从 VC Redist 直接安装 CRT DLL；`windeployqt --qmldir qml`；在无 Qt PATH 的干净会话下启动安装产物并成功播放（`docs/evidence/windows-install-launch.json`） |
| 开始菜单/桌面无快捷方式 | CPack 默认不为 `bin/` 下可执行文件建快捷方式 | 显式创建开始菜单「Palune」文件夹（主程序 + 卸载项）与桌面快捷方式，并配套删除；真实安装校验三处 TargetPath 均正确、卸载后全部清除（`docs/evidence/windows-install-evidence.json`） |
| 许可协议乱码 | 中文 UTF-8 纯文本按安装器代码页解码 | 生成 RTF（`\ansicpg936` + `\uN?` 转义），`CPACK_RESOURCE_FILE_LICENSE` 指向 RTF；渲染比对与源文本逐字一致（`docs/evidence/shots/license-rtf-alpha4.png`） |

校验脚本 `tools/release/verify_windows_install.ps1` 在 release 工作流中对每次 Windows 打包执行同样的真实安装→启动→卸载流程。

## 已完成（M0）

- Windows 主路径：`VideoWindow`（无边框顶层窗口 + Win32 子窗作 `wid`）+ `OverlayWindow`（透明 QML 覆盖层，唯一任务栏/Alt-Tab 入口）
- 播放内核：libmpv 通过 `QLibrary` 运行时加载（`libmpv-2.dll`），`vo=gpu-next` + `gpu-context=d3d11` + `hwdec=d3d11va`
- 最小可跑能力：打开文件、播放/暂停、进度拖动、音量、全屏切换、退出
- 设置页更新模块：检查更新 / 打开主页常驻；发现新版本才出现「更新」「仅下载」；加速镜像列表可切换、可回退直连；SHA-256 + Ed25519 验签，失败拒绝安装
- 版本号校验脚本接入 CI；三平台构建与常规检查
- macOS / Linux：渲染路径只保留接口与配置分支（`MpvClient::backendDescription()` / `#if defined(Q_OS_MACOS)`），**未在本机验证**

## 安装与首次使用（Windows）

### 安装

1. 从 [Releases](https://github.com/yunyu697/palune/releases) 下载 `Palune-<版本>-win-x64.exe` 运行。安装包自带 Qt 运行时与 `platforms\qwindows.dll`，不需要另装 Qt 或 VC 运行库。
2. 安装器在结束前做落盘自检：关键组件缺失时弹中文提示、写出 `install-check.log`、退出码为 3。最常见原因是安全软件拦截了写入，把安装目录加入信任列表后重装即可。
3. 从开始菜单「Palune」或桌面快捷方式启动；卸载走「设置 → 应用」或开始菜单里的卸载项。

### 连接媒体服务器（Jellyfin / Emby）

设置 →「媒体库」页 →「媒体服务器」区块：

1. 类型选 `Jellyfin` 或 `Emby`；极空间、飞牛等兼容 Jellyfin 协议的设备选「其他 Jellyfin（极空间 / 飞牛）」。
2. 登录方式二选一：用户名 + 密码，或 API Key（Jellyfin 控制台 → 高级 → API 密钥）。
3. 填服务器地址（如 `http://192.168.1.10:8096`），先点「测试连接」确认可达，再点「保存并连接」；保存成功后立即拉取媒体库。
4. 密码 / token / API Key 只写入系统凭据存储（Windows 凭据管理器、macOS 钥匙串、Linux libsecret），不落 `settings.toml`；重启后自动回填账号。「删除连接」同时删除系统凭据。
5. Plex 暂无入口：适配器只有骨架，未验证（见「已知限制」）。

### 设置本地媒体库目录

设置 →「媒体库」页 →「添加目录…」选择视频所在文件夹，随后自动递归扫描并生成缩略图（格式过滤，跳过不可读与符号链接；扫描在工作线程，不阻塞播放）。

- 索引默认在 `%APPDATA%\Palune\library\index.json`，缩略图在同级 `thumbs\`；索引损坏会自动重建
- 「重新扫描」用于目录内容变化后刷新；「默认视图」在 海报墙 / 列表 之间切换（`[library] viewMode`）
- 根目录失效或文件被删除后，条目会在下次扫描时清理

## 构建（Windows）

前置：Qt 6.8+（本机验证用 6.8.3 MSVC 2022 64 位）、CMake ≥ 3.21、Visual Studio 2022+。

```powershell
# 1) 恢复运行时依赖（libmpv-2.dll / libsodium.dll，不入库）
powershell -ExecutionPolicy Bypass -File tools/ci/fetch_deps.ps1

# 2) 配置与构建
cmake -S . -B build
cmake --build build --config Release --parallel

# 3) 单元测试
ctest --test-dir build -C Release --output-on-failure
```

产物：`build/Release/Palune.exe`（同目录需有 `libmpv-2.dll`、`libsodium.dll`）。POST_BUILD 会跑 `windeployqt`，构建树自带 Qt 运行时与 `platforms\`，可直接双击运行，不再依赖外部 Qt 前缀。

macOS / Linux 依赖：`tools/ci/fetch_deps.sh`（brew / apt / dnf / pacman）。Qt 与 libmpv 均为动态链接，**禁止静态链接**。

第三方二进制（libmpv、libsodium、CRT、Qt 部署文件）的来源、版本与校验值见 [third_party/MANIFEST.md](third_party/MANIFEST.md)。

## CI

`.github/workflows/ci.yml`：版本号校验 → 分层静态检查 → 三平台构建 + 全部单元测试 + 20 样本 headless 冒烟（`--m1-smoke`，报告作为 artifact 上传）。

- Runner：`windows-latest` / `macos-latest`（当前为 `macos-26-arm64`）/ `ubuntu-latest`
- Qt：三平台统一 `6.9.3`。原因：macOS 26 SDK 移除了 `AGL.framework`，而 Qt 6.8 线最后一版（6.8.3）的 macOS 构建仍带 `-framework AGL`，链接必然失败；Qt 6.9 起已移除该依赖。本地 Windows 仍可用 6.8.3。
- 首次三平台全绿：https://github.com/yunyu697/palune/actions/runs/42216916696

## 发布（release）

源码仓库 `yunyu697/palune` **已公开**，发行产物直接发布在**本仓**的 Release（不再有独立发行仓库）。自 2026-11-08 起更新清单只在 `https://github.com/yunyu697/palune/releases/...`；旧仓 `yunyu697/palune-releases` 已归档、停止发版。完整说明见 [docs/RELEASES.md](docs/RELEASES.md)。

`.github/workflows/release.yml`：三平台各自打包，产出更新清单与校验值。

| 平台 | 产物 |
|---|---|
| Windows | `Palune-<版本>-win-x64.exe`（NSIS 安装器）、`Palune-<版本>-win-x64.msi` |
| macOS | `Palune-<版本>-macos-arm64.dmg` |
| Linux | `Palune-<版本>-linux-x86_64.AppImage`、`.deb`、`.rpm` |

- 触发：打 tag 自动构建并发布到本仓 Release，或手动 `workflow_dispatch`（只构建与生成清单，产物作为 artifact 上传）
- 每次运行都上传 `latest.json` 与 `SHA256SUMS`
- `latest.json` 按 `UPDATER_SPEC.md` 的 payload + 分离签名结构生成；`artifacts` 每平台一条（exe / dmg / AppImage），msi/deb/rpm 放在 `extraArtifacts`
- 清单与产物必须已签名；`manifest` job 自带断言，`signature` 缺失直接失败
- 发布凭据：只用 GitHub 自带 `GITHUB_TOKEN`（`permissions: contents: write`，作用域仅本仓）；不再需要 `RELEASE_REPO_TOKEN` 或任何跨仓 PAT
- 发布后 `assert-public-feed` job 会**不带凭据** GET 本仓 `releases/latest/download/latest.json`，要求 200 且能取到 `version`；401/404 直接失败
- 预发布（alpha / beta / RC）按 **tag 后缀** 判定：`-alphaN` / `-betaN` / `-RCn` 一律 `prerelease=true`，**不接管** `releases/latest`；裸后缀 tag 直接失败；只有正式版 tag（如 `1.0.0`）才发普通 Release
- 最新发布页：https://github.com/yunyu697/palune/releases

### 1.0.0-RC1 当前发布状态（2026-11-14 核对）

| 项 | 值 |
| --- | --- |
| Release | [1.0.0-RC1](https://github.com/yunyu697/palune/releases/tag/1.0.0-RC1)，`prerelease=true`（2026-11-14 由 `1.0.0-RC` 改写，旧 tag 视作 RC1；旧链接已失效） |
| 资产 | `Palune-1.0.0-RC1-win-x64.exe`（**59,757,762 B**，SHA-256 `C893A8D7525D4F5E1471C12ECC7CA1A423FAC51022C9D1D236DBAFDE1407C128`）、`latest.json`、`SHA256SUMS`、`windows-install-evidence.json`、`install-launch.json`、`build-record.json` |
| 重发记录 | 2026-11-13 重写说明并替换资产；2026-11-14 修掉启动前 Qt 英文弹框（外部 `QT_QPA_PLATFORM` 指向不存在的平台插件、插件存在但加载失败两类），并在 DLL 搜索、`qt.conf`、安装器自检（`qt.conf`/CRT）上收口；同日按 RC 序号规则把 `1.0.0-RC` 改写为 `1.0.0-RC1` 重新打包发布（旧 tag 与旧下载链接失效，无跳转）。远端 digest 与本地 `SHA256SUMS` 逐条一致，`releases/latest` 前后均为 **404**，`prerelease=true` 未变 |
| 清单 | `--channel stable`、`version 1.0.0-RC1`、`keyId palune-test`；`--base-url .../releases/download/1.0.0-RC1`；用 `resources/trust/update.pub` 独立验签 PASS（`docs/evidence/release-1.0.0-rc1/manifest-verify.json`） |
| 更新主链 | 本仓 `releases/latest` 当前 **404**（本仓全部 Release 均为 prerelease，尚无正式版；alpha6 的 Latest 标记留在已归档旧仓，不参与本仓主链） |
| 历史 tag 补发 | `1.0.0-alpha8/9/10` 已按原资产、原哈希迁入本仓，均为 prerelease；`1.0.0` 仍只在本地 |
| 未提供 | macOS/Linux 产物、MSI、生产签名（P1 未清） |
| 审计 | `docs/evidence/release-1.0.0-rc/github-publish-audit.json`（旧仓发布记录）、`docs/evidence/release-migration-20261108.json`（迁移核对） |

## 版本号规则（硬约束）

- 唯一来源：`VERSION`；`PALUNE_VERSION` 宏、安装包命名、应用内「关于」与更新清单必须一致；
- 开发期：`1.0.0-alphaN`，N = 1..10，每次只递增 N；`alpha10` 之后转 `beta1`；
- 测试期：`1.0.0-betaN`，N = 1..10；`beta10` 之后转 `RC1`；
- 候选期：`1.0.0-RCn`，n 从 1 开始、**无上限**（RC10 之后可继续 RC11、RC12…），通过后直接发 `1.0.0`；
- **预发布必须带序号**：裸 `1.0.0-RC` / `1.0.0-alpha` / `1.0.0-beta` 一律不合规，不能用一个 tag 反复发新一轮 RC；
- **「除首位外每段 ≤ 10、禁止出现 11」只管 `major.minor.patch`**（`0.11.x`、`1.0.11` 不允许），不约束 RC 计数；alpha/beta 仍限 1..10；
- 校验：`python tools/version/check_version.py`，CI 第一步即执行，不合规直接失败；CI 里 tag 必须与 `VERSION` 逐字一致。

## M0 实测结论（本机 Windows，1 屏 100% 缩放）

报告：[docs/evidence/m0-report.json](docs/evidence/m0-report.json)

| 项目 | 判据 | 结果 | 实测 |
| --- | --- | --- | --- |
| 全屏切换 20 次 | 两窗几何偏移 ≤ 1 帧（16.67ms）、画面不黑 | **PASS** | 最大同步时差 6.284ms；两窗矩形偏移 0px；亮度采样 20/20 非黑（最低 125.19）；`videoReconfig` 0 次 |
| DPI 100/150/200 各拖动 10 次 | 跨屏拖动后零偏移 | **NOT_VERIFIED** | 本机仅 1 个显示器、固定 100% 缩放；已用 `WM_DPICHANGED` 合成消息验证重排后零偏移，但不作为达标证据 |
| Alt-Tab 恢复 | 任务栏/Alt-Tab 只有一个应用窗口，恢复后画面正常 | **PASS** | `appWindowCount=1`；视频窗 `WS_EX_TOOLWINDOW` 隐藏、覆盖窗 `WS_EX_APPWINDOW` 承担身份；最小化/恢复 10 轮后亮度全部非黑 |
| Snap 贴边 | 四种贴边布局 + 最小化/恢复零偏移且画面正常 | **PASS** | 4 种布局 + 10 轮最小化/恢复，最大同步时差 4.2ms，偏移 0px，恢复后亮度采样全部非黑 |

说明：
- Snap 用 `SetWindowPos` 等效几何驱动模拟系统贴边（非鼠标拖拽手势），报告中已注明；
- 测试窗口全部 `WS_EX_NOACTIVATE`，不抢焦点；本机被安全策略拦截 `CreateDesktopW`（错误 87/998），无法用独立桌面隔离，测试在当前桌面以不可激活窗口完成；
- 整体判定为 `PARTIAL`（DPI 一项未验证，不调低标准）。

## 更新模块

- 状态机：`unchecked → checking → uptodate | available → downloading → downloaded → installing | error`
- 仅 `available` 状态显示「更新」「仅下载」；`uptodate` 显示当前版本与上次检查时间
- 「更新」：下载 → SHA-256 + Ed25519 校验 → 安装 → 自动重启；「仅下载」：下载到系统下载目录 → 按钮变「安装」
- **清单也走多源回退**（不再单点依赖 `releases/latest/download`）：顺序为 直连 → `ghfast.top` 实测加速 → `PALUNE_UPDATE_MANIFEST_MIRRORS`（完整 URL、分号/逗号分隔，设置后替代内置镜像）；单源 15 s、整轮 45 s
- **每个清单来源独立验签**：镜像返回被改动的清单会在 Ed25519 校验阶段被丢弃并立即换下一个；全部来源失败才报 `UPD-001`…`UPD-004`
- 安装包下载同样多通道（直连 → 清单 `mirrors[]` → 内置实测加速）；完整性校验**不依赖来源**，任一通道下载的包都必须通过 SHA-256 + 清单签名复核 + 产物签名
- 错误码 `UPD-001`…`UPD-010`，可读文案 + 重试 / 换镜像
- 本机无真实发布产物，用本地静态服务（`python -m http.server 8765`）+ 模拟安装器完成端到端验证

端到端证据（`docs/evidence/`）：

| 文件 | 覆盖场景 |
| --- | --- |
| `report-e2e-full.json` | 更新：checking → available → downloading（镜像 A）→ 校验 → installing（SHA-256 一致） |
| `restart-marker-full.json` | 安装后自动重启并带 `--e2e-restarted` 参数 |
| `report-e2e-download.json` | 仅下载：下载到系统下载目录 `.../Downloads/Palune/`，随后点「安装」→ installing |
| `report-check-badsig.json` | 清单签名错误 → `UPD-004`，拒绝使用 |
| `report-e2e-badhash.json` | 安装包 SHA-256 不匹配 → `UPD-006`，拒绝安装 |
| `shots/update-settings-*.png` | 设置页六状态截图（未检查 / 已是最新 / 发现新版本 / 下载中 / 已下载 / 失败） |

截图命令（窗口不透明度置 0、声明不抢焦点，`grabWindow()` 走 FBO 取帧，不占前台不弹窗）：

```powershell
build\Release\Palune.exe --ui-shot qa\runtime\shots
```

## M0 自测

```powershell
build\Release\Palune.exe --m0-selftest --report qa\runtime\m0-report.json --video qa\runtime\media\testsrc2-720p.mp4
```

退出码：`0` = 全部 PASS，`2` = 存在 FAIL / 未验证项。

## 目录结构

```
src/app/        进程入口与运行模式（正常 / M0、M1 自测 / 截图 / 更新 e2e）
src/window/     双窗架构：VideoWindow（视频承载）+ OverlayWindow（QML 界面）+ WindowController（几何与 Z 序同步）
src/mpv/        libmpv 动态加载封装
src/settings/   settings.toml 读写、窗口几何、最近文件
src/theme/      ThemeRegistry（主题选择/回退/持久化）+ LayoutHost（布局子树重建）
src/ui/         UiStateStore（滚动、选中、面板宽度、chrome 可见性）
src/media/      MediaLibraryService（扫描/索引/缩略图）+ 内容源接口 + Jellyfin/Emby/Plex 适配器与 MediaServerHttp
src/core/       路径、启动计时、进程内存指标、CredentialStore（系统凭据存储）
src/diag/       日志与崩溃面包屑
src/update/     版本号、更新清单信封、签名与哈希校验、下载/安装状态机
src/m0/         M0 自测（全屏/DPI/Alt-Tab/Snap/命中）
qml/            tokens / components / layouts / shell / library 与主界面、设置页
tests/unit/     版本、清单、settings、主题、内容源抽象测试
tools/          版本校验、分层检查、样本生成、依赖准备、自测运行、e2e 模拟服务
docs/evidence/  实测报告与截图
```

## 已知限制（1.0.0-RC）

- **媒体服务器**：Jellyfin / Emby 已可用（Windows 真机完成连接 → 拉库 → 播放 → 进度回写）；**Plex 未完成**（仅适配器骨架，无设置入口，未验证）；极空间 / 飞牛只能走「其他 Jellyfin」，未在真机验证；macOS Keychain / Linux libsecret 代码路径已实现，未实机验证
- **HDR/色调映射曲线**：设置页不画 BT.2390 曲线（缺真实数据源），只显示“曲线预览不可用”，不是占位图
- **英文界面**：`en_US` 覆盖历史文案；本轮新增的设置文案在英文下回退中文（PARTIAL）
- **设置行为取证**：十项中 8 项 DONE，3 项 NOT_VERIFIED（下载真实落盘位置、外部许可查看器打开、beta manifest 真实拉取），见 `docs/SETTINGS-GAP.md` §10.2
- **平台**：macOS/Linux 只有 CI 构建与冒烟，**未在真机安装运行**；MSI 未产出（本机无 WiX）；**签名使用测试密钥**（`update.pub` 与测试私钥同对，不是生产签名）
- **跨屏 DPI**：单显示器环境，跨屏拖动与运行时 DPI 切换只能模拟
- **冷启动**：安装包实测 1878ms，高于 800ms 基线（P3，未修）
- **更新链（504 出口限制）**：`releases/latest/download/*` 在本机与独立出口均返回 GitHub 边缘 504（alpha6 历史资产同样如此），**受影响出口上直连更新链不可用，只能依赖第三方镜像 `ghfast.top`**。客户端已加清单与安装包多源回退（直连 → `ghfast.top`）；但该镜像对 tag 定点 RC 清单同样 504，镜像不可达时更新链仍不可用。是否全球影响 **NOT_VERIFIED**：GitHub 状态页显示 All Systems Operational 且无相关事故，未提供旁证（见 `docs/evidence/release-1.0.0-rc/feed-504-diagnosis.json`）
- **源码仓 tag / 发行仓 Release**：源码仓 alpha1/4/5/7/8/9/10 已在远端；发行仓 alpha8/9/10 已手动补发 prerelease（不接管 `releases/latest`）；`1.0.0` 暂留本地，等 RC 收口后按正式通道处理（历史 tag 的提交带旧工作流，推送时须先停用 Release 工作流）

## 明确不做（M0 与文档排除项）

位图皮肤、3D/VR、软解滤镜矩阵、8K 承诺。

## 许可

见 [NOTICE.md](NOTICE.md)。libmpv 为 LGPL-2.1-or-later，动态链接并按许可要求提供可替换性。
