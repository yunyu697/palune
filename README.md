# Palune

轻量、无位图皮肤、面向 Windows 优先的桌面视频播放器。M2 阶段：三主题验收矩阵（36 界面基线、50 次切换状态保持、紧凑降级、配置回退）与 Windows 安装器修复。

当前版本：**1.0.0-alpha4**（版本号唯一来源是仓库根目录 `VERSION`）。

## 已完成（M1）

- 四层主题：`qml/tokens`（L0 令牌）→ `qml/components`（L1 组件，只发信号）→ `qml/layouts/LayoutA|B|C`（L2 布局，只读播放器属性并调用 `win.action`）→ `ThemeRegistry` + `LayoutHost`（L3，只重建布局子树）
- `VideoHost` 位于布局树外，切换布局不触碰视频承载窗口；主题持久化到 `settings.toml` 的 `[ui] theme`，缺键/非法值/资源缺失均回退 A
- 最小 UI：打开文件/文件夹、拖拽、播放暂停、seek、音量、速度、音轨/字幕轨、全屏、退出、最近文件、三套布局
- `settings.toml` 原子读写（保留未知键）、窗口几何恢复、`UiStateStore` 骨架
- 内容源抽象：五组接口（认证/列表/取流/上报/能力）+ `MediaItem`/`UserState` + 适配器注册表，媒体库空状态页
- 分级日志、崩溃面包屑（`crashes/breadcrumbs.txt` 与致命信号快照）、版本信息页
- 更新清单升级为 payload + 分离签名信封，按 platform/arch 选择产物并校验产物签名
- 命名与标识冻结：`app.palune.player`、`%APPDATA%\Palune`、日志前缀 `palune`、`Palune/crashes`、`palune://`、`https://updates.palune.app/{stable,beta}/latest.json`
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
| 覆盖率 | ≥70% | **待验证** | 已补 `tst_core` 与关键路径过滤，等待新 CI 出数 |
| 20 样本 × 3 OS 冒烟 | macOS/Linux 各 ≥19/20 | **待验证** | 本机仅有 Windows；CI headless 冒烟结果为准 |
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

产物：`build/Release/Palune.exe`（同目录需有 `libmpv-2.dll`、`libsodium.dll`）。

macOS / Linux 依赖：`tools/ci/fetch_deps.sh`（brew / apt / dnf / pacman）。Qt 与 libmpv 均为动态链接，**禁止静态链接**。

## CI

`.github/workflows/ci.yml`：版本号校验 → 分层静态检查 → 三平台构建 + 全部单元测试 + 20 样本 headless 冒烟（`--m1-smoke`，报告作为 artifact 上传）。

- Runner：`windows-latest` / `macos-latest`（当前为 `macos-26-arm64`）/ `ubuntu-latest`
- Qt：三平台统一 `6.9.3`。原因：macOS 26 SDK 移除了 `AGL.framework`，而 Qt 6.8 线最后一版（6.8.3）的 macOS 构建仍带 `-framework AGL`，链接必然失败；Qt 6.9 起已移除该依赖。本地 Windows 仍可用 6.8.3。
- 首次三平台全绿：https://github.com/yunyu697/palune/actions/runs/42216916696

## 发布（release）

`.github/workflows/release.yml`：三平台各自打包，产出更新清单与校验值。

| 平台 | 产物 |
|---|---|
| Windows | `Palune-<版本>-win-x64.exe`（NSIS 安装器）、`Palune-<版本>-win-x64.msi` |
| macOS | `Palune-<版本>-macos-arm64.dmg` |
| Linux | `Palune-<版本>-linux-x86_64.AppImage`、`.deb`、`.rpm` |

- 触发：打 tag 自动构建并创建 Release（预发布），或手动 `workflow_dispatch`（产物仍会作为 artifact 上传，便于无 tag 验收）
- 每次运行都上传 `latest.json` 与 `SHA256SUMS`
- `latest.json` 按 `UPDATER_SPEC.md` 的 payload + 分离签名结构生成；`artifacts` 每平台一条（exe / dmg / AppImage），msi/deb/rpm 放在 `extraArtifacts`
- 签名私钥取仓库 Secret `PALUNE_UPDATE_PRIVATE_KEY`（PEM 或其 base64，或 32 字节种子 base64），缺失时清单标记 `signing.signed=false` 且产物不带签名，流程不因此变红
- 首次完整跑通（tag `1.0.0-alpha1`，三平台打包 + Release）：https://github.com/yunyu697/palune/actions/runs/42238311152
- 发布页：https://github.com/yunyu697/palune/releases/tag/1.0.0-alpha1

## 版本号规则（硬约束）

- 开发期：`1.0.0-alphaN`，每次推进只递增 `N`；
- **除第一段外任何数值段 ≤ 10，禁止出现 11**（alpha 编号超过 10、minor/patch 超过 10 都不允许）；
- `alpha10` 之后转 `1.0.0`；
- `VERSION` 是唯一来源，构建配置（`PALUNE_VERSION` 宏）、安装包命名、应用内「关于」与更新清单示例必须一致；
- 校验：`python tools/version/check_version.py`，CI 第一步即执行，不合规直接失败。

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
- 镜像只是下载通道；完整性校验**不依赖镜像来源**，任一镜像下载的包都必须通过 SHA-256 与签名
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
src/media/      内容源抽象（接口、Item/UserState、适配器注册表）
src/core/       路径、启动计时、进程内存指标
src/diag/       日志与崩溃面包屑
src/update/     版本号、更新清单信封、签名与哈希校验、下载/安装状态机
src/m0/         M0 自测（全屏/DPI/Alt-Tab/Snap/命中）
qml/            tokens / components / layouts / shell / library 与主界面、设置页
tests/unit/     版本、清单、settings、主题、内容源抽象测试
tools/          版本校验、分层检查、样本生成、依赖准备、自测运行、e2e 模拟服务
docs/evidence/  实测报告与截图
```

## 明确不做（M0 与文档排除项）

位图皮肤、3D/VR、软解滤镜矩阵、8K 承诺。

## 许可

见 [NOTICE.md](NOTICE.md)。libmpv 为 LGPL-2.1-or-later，动态链接并按许可要求提供可替换性。
