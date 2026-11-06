# Palune

轻量、无位图皮肤、面向 Windows 优先的桌面视频播放器。M0 阶段：Qt 6 + QML 骨架，libmpv **动态链接**，窗口内嵌播放区。

当前版本：**1.0.0-alpha1**（版本号唯一来源是仓库根目录 `VERSION`）。

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

`.github/workflows/ci.yml`：先跑版本号校验，再并行构建三平台并跑单元测试（Windows / macOS 跑版本号规则，Linux 额外跑更新清单签名与哈希）。

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
src/app/        进程入口与运行模式（正常 / 自测 / 截图 / 更新 e2e）
src/window/     双窗架构：VideoWindow（视频承载）+ OverlayWindow（QML 界面）+ WindowController（几何与 Z 序同步）
src/mpv/        libmpv 动态加载封装
src/update/     版本号、更新清单、签名与哈希校验、下载/安装状态机
src/m0/         M0 自测（全屏/DPI/Alt-Tab/Snap/命中）
qml/            主界面与设置页
tests/unit/     版本号规则、更新清单解析测试
tools/          版本校验、依赖准备、自测运行、e2e 模拟服务
docs/evidence/  实测报告与截图
```

## 明确不做（M0 与文档排除项）

位图皮肤、3D/VR、软解滤镜矩阵、8K 承诺。

## 许可

见 [NOTICE.md](NOTICE.md)。libmpv 为 LGPL-2.1-or-later，动态链接并按许可要求提供可替换性。
