# Palune 开发状态（断点）

> 自动推进的续跑点。每完成一个可验证节点更新本文件；撞到单轮上限时以本文件为准。

更新时间：2026-11-08（Asia/Shanghai）
当前里程碑：**alpha8 实机 UI 缺陷清理（在 M4 之前）**
版本：`1.0.0-alpha8`

## alpha8：实机四条现象根因修复

### 根因与修复

| 现象 | 根因 | 修复 | 证据 |
|---|---|---|---|
| 画面内方框 | 覆盖窗 `WS_THICKFRAME` 触发 Windows 非客户区合成层 | `SetWindowSubclass`：`WM_NCCALCSIZE(TRUE)` 返回 0，`WM_NCACTIVATE/WM_NCPAINT` 返回 TRUE | `probe_square.py` 修复后 center_box=None；`UI-ALPHA8-COMPARISON.md` §1 |
| 窗口边缘方框 | 同上，失焦时系统重画浅色 NC 边框 | 同上（保留 THICKFRAME，缩放/Snap 不受影响） | `probe_focus_border.py`：边缘由 (255,245) 变 (26,26) |
| 最大化/全屏方框 | 同上，几何变化后 NC 重绘、Region 变 SIMPLE | 同上 | `state-max-A.png` / `state-full-A.png` 边缘为内容色 |
| 设置被遮挡/裁剪 | 设置是主窗内弹层 | 新增顶层 `SettingsWindow`（Qt::Tool + 瞬态父窗 + 原生圆角 Region）；C 改为 show 后定位，贴右下 16px | `settings-{A,B,C}.png`；关闭后可见窗口只剩覆盖窗+视频窗 |

同时：A 顶栏改为设计稿的透明悬浮条；B 检查器加 12px 外边距与 8 圆角；`Main.qml` 发丝描边降到 `#1affffff` 且最大化/全屏不绘制。

### 验证

| 项 | 结果 |
|---|---|
| 单元测试 | `ctest -C Release` **7/7 PASS** |
| 实机逐屏 | 21 状态（main/empty/library/settings × A/B/C、max/restore/full/full-exit/move/resize、DPI 100/150/200），`docs/evidence/shots/alpha8-ui/` |
| 方框体检 | 全部状态边缘无浅色框、无中心方框（`alpha8_edge_audit.py`） |
| 设计稿核对 | `docs/evidence/UI-ALPHA8-COMPARISON.md`：PASS 项 + 5 条如实记录的剩余形态差异 |
| 限制 | 会话锁屏，真实指针拖动/贴边/实时 DPI 无法注入；移动/缩放用 `SetWindowPos`、DPI 用 `QT_SCALE_FACTOR` 重启模拟 |
| 本地安装包 | 见下方「alpha8 收口记录」 |

### alpha8 收口记录

| 项 | 值 |
|---|---|
| 安装包 | `dist/Palune-1.0.0-alpha8-win-x64.exe` |
| 字节数 | 59,218,560 |
| SHA-256 | `F763585FA65613728DD15A5F11D18AF1B9CAC2DCB4E0C8D77A1D9BC1BF349D67` |
| 安装→启动→卸载 | `PASS`（`docs/evidence/install-verify-alpha8.json`、`install-launch-alpha8.json`；windowReadyMs 1466，alpha7 为 1481，无回归但未达 800ms 基线） |
| MSI | 本机未安装 WiX（candle.exe），本轮跳过，仅出 NSIS；`package_windows.ps1` 已改为无 WiX 时告警跳过 |

## alpha7：M3 UI 结构收口（设计稿逐屏对齐）

按既定工作方式自动推进：本地构建/验证、不打 Release、不推跨仓产物、不依赖线上更新仓库。

### 修复项（对照 alpha5 §3 的 7 项偏差）

| # | 偏差 | 实现 | 证据 |
|---|---|---|---|
| 1 | 媒体库形态 | `LibraryPanel` 按主题分支：A 右侧 352 滑出侧栏（不压暗，自 104px 起）；B 居中宽面板；C 右侧 76% 抽屉。A 打开时底部控制条右边界收至 368 | `docs/evidence/shots/alpha7-matrix/m2-{A,C}-library.png` |
| 2 | C 默认几何 | `WindowController::start`：无保存几何且主题 C → 1100×680 | `ui-alpha7-window-geometry.json`：覆盖窗 2200×1360 物理 |
| 3 | B 视频让位 | 新增 `setVideoInsets()`；B 按检查器宽度内缩视频窗；检查器改不透明 | 同上 JSON：B 视频 1720×1400，右内缩 640 物理 / 320 逻辑 |
| 4 | B 技术条 | `MpvClient` 新增 `videoCodec/videoFps/videoBitrate/hwdec/hdrMode`；映射 mpv 属性，`video-bitrate` 缺失时用 `file-size/duration` 折算 | `shots/alpha7-ui/main-B.png`：H.264 · d3d11va · 30.000 fps · 5.50 Mbps · SDR |
| 5 | 设置形态 | A 1020×660 双栏；B 1080×660 三栏（字幕预览 + BT.2390 曲线）；C 右下 560×452 弹出卡（两栏） | `m2-{A,B,C}-settings-general.png` 等 18 张 |
| 6 | 空态规则 | A 隐藏标题/控制条（保留右上窗口按钮）；B 隐藏技术条、检查器 `—`；C 仅窗口按钮 | `shots/alpha7-ui/empty-{A,B,C}.png` |
| 7 | 媒体库数据 | 内容源未接入 | **NOT_VERIFIED**，转 M4 |

### 验证

| 项 | 结果 |
|---|---|
| 单元测试 | `ctest -C Release` **7/7 PASS** |
| 版本/品牌检查 | `check_version.py` OK（并修正证据日志里 GitHub Cookie 的误报）；`check_brand_terms.py` 0 命中 |
| 实机截图 | 6 张原生窗口合成（3 主题 × 主界面/空态）+ 39 张自渲染矩阵（13 屏 × 3 主题），对照文档 `docs/evidence/UI-ALPHA7-COMPARISON.md` |
| 本地安装包 | `dist/Palune-1.0.0-alpha7-win-x64.exe`，59,214,724 B，SHA-256 `dc3a8216ec668261a26a0591b91ca0099fa17e0b2e06ca95f8248647f6ee17df` |
| 安装→启动→卸载 | **PASS**（`docs/evidence/windows-install-evidence.json`、`install-launch.json`，version=1.0.0-alpha7） |
| 发布 | tag `1.0.0-alpha7` 已推送；按既定工作方式**不创建 Release**：tag 触发的 Release 工作流已主动取消（run 42386155959 结论 cancelled），发行仓库仍只有 alpha6 |
| 判定 | **PASS**（偏差 #7 NOT_VERIFIED，不阻塞） |

### 下一阶段

**M4**：媒体库真实内容源（扫描/索引/海报/列表与表格形态），并继续每里程碑「截图 × 设计稿」对照。


## alpha6：私有源码 + 公开发行仓库（更新链路重构）

用户要求解决「私有仓库 Release 资产无法匿名下载 → 检查更新 401/404、镜像方案失效」的结构性问题。

### 事实核查（先纠正三个前提）

| 前提 | 实际情况 |
|---|---|
| 源码仓库是私有的 | **当时是 PUBLIC**（`gh repo view` 返回 `visibility=PUBLIC`）。已在 18:41 转为私有，随后匿名访问 tag 资产与 latest 均为 404 |
| `latest` 404 是私有权限导致 | 不全是。GitHub 的 `releases/latest` **只指向非预发布 Release**；alpha5 是 prerelease，所以公开状态下 tag 直链 `releases/download/1.0.0-alpha5/latest.json` 返回 200，而 `releases/latest/download/latest.json` 返回 404 |
| 清单签名链路可用 | **不可用**：alpha5 的 `latest.json` 是 `signature=null` / `signing.signed=false`（仓库此前没有任何 Actions Secret）；而客户端 `UpdateManifest::parse` 明确拒绝未签名清单，更新必然失败 |

### 改造内容

| 项 | 内容 |
|---|---|
| 公开发行仓库 | 新建 `yunyu697/palune-releases`（public，仅发行物、不含源码），README 写明定位、更新源、校验方式、回退方式 |
| 源码仓库 | `yunyu697/palune` 转为私有；跨仓写操作只在 CI 发生 |
| 跨仓发布 | `release.yml` 的 publish job 改用 `RELEASE_REPO_TOKEN`（仅 `palune-releases` 的 `contents:write`，存私有仓库 Secret）；GitHub 自带 `GITHUB_TOKEN` 无法跨仓，已在 job 内做缺失断言 |
| 匿名断言 | 新增 `assert-public-feed` job：不带凭据 GET `releases/latest/download/latest.json`，要求 200 且 `payload.version` 等于本次发布、清单已签名，并对每个安装包 URL 匿名 HEAD 200；401/404 直接失败，原始响应头打印进日志 |
| 签名断言 | `manifest` job 新增断言：清单签名与每个产物签名必须存在，缺一直接失败（修复「未签名也能发布」的静默放行） |
| 客户端更新源 | 默认改为 `https://github.com/yunyu697/palune-releases/releases/latest/download/latest.json`，不带任何凭据 |
| 镜像逻辑 | 镜像只用于安装包下载，清单与签名始终走主链；下载失败或内容校验不通过自动切下一条（直连→镜像 1→镜像 2），全失败才报错；任何通道的包都必须通过 SHA-256 + 清单签名 + 产物签名三重校验 |
| 签名密钥 | 使用仓库内既有的本地测试私钥（`tools/update/test-key.pem`，公钥已内置于 `resources/trust/update.pub`）写入 Secret `PALUNE_UPDATE_PRIVATE_KEY` / `PALUNE_UPDATE_KEY_ID=palune-test`；未新造密钥。stable 前必须换离线生产密钥 |

### 文档

- `docs/RELEASES.md`：两仓关系、凭据类型与权限范围、发布流程、匿名断言、回退方式、为什么不能内嵌 token、镜像策略。
- `README.md` 发布章节同步更新。

### 验证（alpha6，证据见 `docs/evidence/UPDATE-CHAIN-ALPHA6.md`）

| 用例 | 结果 |
|---|---|
| 匿名 GET `releases/latest/download/latest.json` | **200**（首跳 302 → 最终 200，落到 Azure Blob），`version=1.0.0-alpha6`，已签名 `keyId=palune-test` |
| 匿名下载安装包 | **200**，59,210,046 B |
| SHA-256 | 实测 `2d146581…eb981` = 清单值，一致 |
| 清单签名 / 安装包签名 | Ed25519 **均 PASS**（内置公钥验证） |
| 客户端真实检查（`--update-check`） | `state=uptodate`、`availableVersion=1.0.0-alpha6`、`errorCode` 空（未签名会报 UPD-004） |
| 安装 → 启动 → 卸载 | **PASS**（本地构建包，出包前后同一文件上传） |
| CI 断言正向 | 公开仓库 URL → 200、version 一致、产物 HEAD 200 → exit 0 |
| CI 断言负向 | 私有仓库同路径 → 404 → `::error::` → exit 1 |
| 镜像投毒（哈希不符） | 自动切换 镜像 A → 镜像 B，被投毒内容未落盘，最终哈希 = 镜像 B |
| 镜像 404 | 自动切换 镜像 A → 镜像 B，下载成功 |

发布页：<https://github.com/yunyu697/palune-releases/releases/tag/1.0.0-alpha6>（资产：exe、latest.json、SHA256SUMS、安装验证证据）。

### 卡点（需用户操作）

| 卡点 | 影响 | 解除方式 |
|---|---|---|
| `RELEASE_REPO_TOKEN` 未配置 | CI 的 `publish` 与 `assert-public-feed` job 无法执行；本次 alpha6 由本机用已授权的 `gh` 凭据一次性发布 | 用户签发**仅对 `yunyu697/palune-releases` 有 `Contents: Read and write`** 的细粒度 PAT（或 GitHub App token），执行 `gh secret set RELEASE_REPO_TOKEN -R yunyu697/palune`；随后推送 tag `1.0.0-alpha6` 重跑三平台构建与发布（CI 用 Qt 6.9.3，哈希会与本机不同，需重跑匿名断言） |
| 未创建任何新 token | —— | 严格遵守「不自己造 token」，跨仓凭据只能由用户签发 |

补充：alpha6 仅发布 Windows 安装包；MSI 因本机无 WiX 未生成（CI 有 WiX），macOS/Linux 产物需 CI 补齐。


## alpha5：P0 可用性修复 + 图标 + 实机逐屏对照

用户实测 alpha4「能启动、能显示，但不可用」，本轮按 P0 处理。**根因与修复（每条均有实机证据）**：

| 现象 | 根因 | 修复 | 证据 |
|---|---|---|---|
| UI 显示不全、按钮被截断 | `SetWindowPos` 收物理像素、Qt 矩形是逻辑像素，DPR=2 下窗口只有一半 | `applyGeometry()` 按 `devicePixelRatio` 换算；视频子窗同步 | `GetWindowRect` = `2360×1400`；QML 树 `Main 1180x700` |
| 白块 | `STATIC` 子窗白底按逻辑尺寸只覆盖 1/4；空闲 mpv 子窗露浅色底 | 自定义黑底窗口类 `PaluneVideoSurface` + 原生客户区尺寸；空闲隐藏视频面 | `shots/alpha5-matrix` 36 张无白块 |
| 进度条/按钮被遮挡 | `SeekBar` 的 `Item` `implicitHeight=0`，Slider 溢出；整窗 `MouseArea` 盖住布局按钮 | 自绘 2px 轨道 + 9px 点、`implicitHeight: 24`；整窗点击区移到布局之前 | 12 张实机截图 |
| 无法移动 / 无法缩放 | 无边框窗无系统非客户区 | `startSystemMove()` + 四边四角 8px `startSystemResize()` | `docs/evidence/window-drag-resize.json`：drag 200×100、resize +300，PASS |
| 方角 | 无边框透明窗下 DWM 圆角不生效 | `CreateRoundRectRgn` + `SetWindowRgn`，半径随主题 | `docs/evidence/window-radius.json`：A/B/C = 27/20/41 物理（14/10/20 逻辑），PASS |

**逐屏对照**：`docs/evidence/UI-ALPHA5-COMPARISON.md`（3 主题 × 空态/播放/设置/列表 = 12 屏，逐屏列设计期望、实机、判定）。
- 截图：`docs/evidence/shots/alpha5-ui/`（12 张实机 `CopyFromScreen`）+ `docs/evidence/shots/alpha5-matrix/`（36 张 `grabWindow` 自渲染基线）。
- 窗口回归：`qa/runtime/m2-window-alpha5.json` 20 项 **PASS**（跨屏移动、运行中 DPI 切换 2 项 NOT_VERIFIED，需真机）。
- 已知偏差 7 项（媒体库形态、C 默认尺寸、B 视频分栏、B 技术参数、设置分区、空态隐藏规则、媒体库无数据），已在对照文档 §3 登记，列入 M3。

**截图中的外部元素**：右下白色方框与左上角 FPS 文本是本机第三方远控叠加层（`GameViewerServer`），不在 QML 树、不在应用窗口枚举、`grabWindow` 矩阵中完全干净，与本应用缺陷无关。

**应用图标（方向 A「折角 Fold」）**：`tools/release/gen_icon.py` 纯标准库生成 `packaging/palune-256.png` + `palune.ico`（16/24/32/48/64/256，32bpp DIB 条目）。缺角由透明改为设计稿同款深色底 `#0C0E11`；与设计成品 `a-512-win-dark.png` 512×512 像素比对差异 **0.54%**（仅折线边缘抗锯齿）。exe 资源图标已接入（`packaging/palune.rc` + `app.setWindowIcon`），`ExtractAssociatedIcon` 复核：底板 `#E7B27C`、缺角 `#0C0E11`、A=255，均为设计色。

### 流程固化（长期）

**每个里程碑收口前必须提交「实机截图 × 设计稿逐屏对照」，缺对照不算通过。**
- 覆盖范围：至少 主界面 / 设置 / 列表 / 空态 × 全部主题；每屏列设计期望、实机、判定。
- 落盘位置：`docs/evidence/UI-<版本>-COMPARISON.md`，截图放 `docs/evidence/shots/<版本>-ui/`。
- 判定口径：符合 / 偏差（登记编号与处置里程碑）/ 未验证（写原因），不允许静默省略。

## alpha5 发布（GitHub Releases，源码仓库私有前）

- 触发：tag `1.0.0-alpha5`，run 42346768448 六个作业全部 success。
- 资产：6 个安装包 + `latest.json`(2900B) + `SHA256SUMS` + 安装验证/启动证据；**没有 `.sig` 资产**（签名本应内嵌在 latest.json，但实际未签名）。
- 复核：从 Release 重新下载 `Palune-1.0.0-alpha5-win-x64.exe`（60,182,151B），SHA-256 = `17d202b0efd19aa70ae50c4357a68accf403ec5d68a53b181faae0ac63371f01`，与 `SHA256SUMS` 完全一致。
- 注意：该 Release 为 prerelease，`releases/latest/download/latest.json` 当时即 404；且清单未签名。alpha6 起改由公开的 `palune-releases` 承载。

## alpha4 发布（GitHub Releases）

**结论：alpha4 之前只在本地产出，从未进入 Releases。** 排查结果：

| 环节 | 实际情况 |
|---|---|
| `ci.yml` 产物 | 只有 `coverage`、`m1-smoke-Windows/Linux/macOS` 报告（772~9128 字节），**不含安装包**；保留期到期即失效 |
| `release.yml` 触发 | 只监听 `push: tags: ["*"]` 与 `workflow_dispatch`；alpha2~alpha4 期间**从未推送 tag**，故流水线一次都没跑 |
| Release 现状 | 只有 `1.0.0-alpha1`（tag `1.0.0-alpha1`），无 alpha2/3/4 |
| publish job | `if: startsWith(github.ref, 'refs/tags/')`——手工 `workflow_dispatch` 只会构建+生成清单，**不会创建 Release** |
| 本地包 | `Palune-1.0.0-alpha4-win-x64.exe`（07:04 构建，07:07 完成安装验证），仅存在于临时工作目录 |

**处置**：修复 `check_version.py` 对本地 `build*`/`dist` 目录的误报；推送 tag `1.0.0-alpha4`，触发三平台打包 → 清单生成 → Release 发布。过程中又修掉两处断点：

1. macOS 打包失败：`qt_generate_deploy_app_script` 传了 Qt 6.9 已删除的 `--compiler-runtime`，`macdeployqt` 报 Unknown argument。
2. Release 发布失败：`dist/*` 含 Windows 验证输出目录 `dist/verify`，`gh release upload` 拒绝目录；改为只上传 `dist` 顶层文件。

**最终发布（run 42337180135 全部 success）**：<https://github.com/yunyu697/palune/releases/tag/1.0.0-alpha4>

| 资产 | SHA-256 |
|---|---|
| `Palune-1.0.0-alpha4-win-x64.exe`（60,177,283 B） | `101b50526c69cc82c777b82b5c4bdd33d260bed42a5982675441d456518dce97` |
| `Palune-1.0.0-alpha4-win-x64.msi`（81,116,446 B） | `105a4a8a571cac6a396fcd30ed4ed228be18cb2914fd6e7c09948826972d1abc` |
| `Palune-1.0.0-alpha4-macos-arm64.dmg`（68,035,794 B） | `83bebe395c7019c4406475fa09306ac222fc9f3b47b552f056d57b947e0f0f69` |
| `Palune-1.0.0-alpha4-linux-x86_64.AppImage`（124,566,008 B） | `192374b32f064c445a42415e6743c20d7a099e859f468c7d630c2e8bf4ba84a2` |
| `Palune-1.0.0-alpha4-linux-x86_64.deb`（108,548,948 B） | `6aeaa0f5f128b9b2fe80d55080cda001a6205a644597f75d4be5fc3ceb9af0d2` |
| `Palune-1.0.0-alpha4-linux-x86_64.rpm`（132,467,057 B） | `cd96f20e1ae54ff9d5f71918b924deff74dfb0c6b21084707fc03d9ec520a460` |

另附 `latest.json`、`SHA256SUMS`、`windows-install-evidence.json`、`install-launch.json`。

**发布包与本地验证包的哈希对照（不一致，原因已核实）**：

| 包 | 大小 | SHA-256 |
|---|---|---|
| 本地 07:04 构建（Qt 6.8.3，已验证） | 59,153,598 B | `092322e611b2876344a576a189b1992f30f97c63f8f8f1681ee27d028ce901ed` |
| Release 发布（Qt 6.9.3，CI 构建） | 60,177,283 B | `101b50526c69cc82c777b82b5c4bdd33d260bed42a5982675441d456518dce97` |

差异来源：Qt 版本不同（本地 6.8.3 / CI 6.9.3）、libmpv DLL 来源不同、NSIS 安装包内嵌时间戳，均导致字节不一致。**发布的不是未验证构建**：release 流水线的 Windows job 对这份最终 exe 做了「安装 → 启动 → 卸载」全流程验证，`windows-install-evidence.json` 显示 `installExitCode=0`、三个快捷方式正确、`launch.ok=true`、卸载后安装目录与快捷方式全部清除、`verdict=PASS`；该证据与安装包同批发布。

**复核**：从 Release 重新下载 `Palune-1.0.0-alpha4-win-x64.exe`（60,177,283 B），本地计算 SHA-256 = `101b50526c69cc82c777b82b5c4bdd33d260bed42a5982675441d456518dce97`，与 Release 内 `SHA256SUMS` 完全一致，下载链路无损。

### 基线

| 项 | 状态 |
|---|---|
| 基线 commit | `01da2e2`（已推送 `origin/main`；tag `1.0.0-alpha4` 指向该提交） |
| CI（f7fe72f） | **三平台全绿**：版本/分层/覆盖率/Windows/Linux/macOS 全部 success；macOS 冒烟 20/20 样本 PASS |
| Release（alpha4） | **已发布**：run `42337180135` 六个作业全部 success，6 个安装包 + 清单/校验值/安装证据已上传 |
| macOS SIGBUS 根因 | lldb 栈：`mpv_create → AppHub.init → RemoteCommandCenter.init → AppHub.getIcon → NSImage(data:) → ImageIO PNGReadPlugin`，PC 跳到 `0xbad4007`。根因是 `DYLD_LIBRARY_PATH` 含 `$(brew --prefix)/lib`，Homebrew 的 libpng/libjpeg 抢占了系统 ImageIO 的解码插件；收窄为 `libsodium/lib:mpv/lib` 后消失 |
| CI（1d5d306） | 仅 macOS 失败（上述 SIGBUS），其余全绿 |
| 本机 8h 浸泡 | **PASS**：`qa/runtime/m2-soak-8h.json`（480 分钟，RSS 242.6→131.8MB，漂移 -13.85MB/h，崩溃 0） |

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
| 7 | 强杀 10 次续播 ±1s；8h 浸泡 | **PASS**：强杀 `m2-resume-kill.json`（10 轮，delta 0~0.067s）；8h 浸泡 `m2-soak-8h.json`：480 分钟、RSS 242.6→131.8MB（漂移 -13.85MB/h，无增长）、崩溃 0、`verdict=PASS` |
| 8 | 窗口行为 20 项清单 | **部分通过**：`m2-window.json` 20 项，18 PASS / 2 NOT_VERIFIED（跨屏移动、运行中 DPI 切换，需真机）；Alt-Tab/任务栏、Snap 样式、全屏、最小化恢复、几何持久化均 PASS |
| 9 | 中/英 i18n + 无硬编码文案 lint | **PASS**：`m2-i18n.json`；QML 125 处 qsTr、10 上下文 203 条翻译全完成，硬编码 0；中/英各 37 张截图（`shots/m2-i18n-zh|en`） |

## 下一步（按顺序）

1. **等用户配置 `RELEASE_REPO_TOKEN`** → 推送 tag `1.0.0-alpha6`，让 CI 在本机已发布的同一 tag 上重建三平台产物、发布到 `palune-releases`、跑匿名断言；下载复核哈希并重跑签名校验（CI Qt 6.9.3 与本机 Qt 6.8.3 字节不同）。
2. 用 alpha6 安装包做一次真实安装 → 启动 → 逐屏截图复核（重点：拖拽、缩放、圆角、白块、按钮可点）。
3. M3 首个 UI 任务：按 `UI-ALPHA5-COMPARISON.md` §3 的 7 项偏差逐个改（媒体库形态 → C 默认尺寸 → B 分栏/技术条 → 设置分区 → 空态隐藏）。
4. M2 #8 剩余真机项：多屏移动、运行中 DPI（125/150/200%）切换需用户环境；自动化部分已完成。
5. stable 前：轮换离线生产签名密钥并替换 `resources/trust/update.pub`。

## 阻塞 / 需用户

- **`RELEASE_REPO_TOKEN` 未配置**：CI 跨仓库发布与匿名断言未在 CI 中真实执行；需用户签发仅含 `palune-releases` `contents:write` 的细粒度 PAT。
- M2 #1 36 界面评审签字需用户确认（截图位于 `docs/evidence/shots/m2-baseline`、`m2-scale`）。
- 生产签名证书、域名、真机多屏/DPI 环境：alpha 阶段按测试密钥、NOT_VERIFIED 处理，不阻塞推进。
