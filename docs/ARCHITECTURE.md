# Palune — 跨平台播放器技术方案

> 产品名：**Palune** · 类型：技术方案（评审稿） · 版本 v1.0 · 日期 2026-11-06
> 目标平台：Windows 10 1809+ / macOS 13+ / Linux（X11 与 Wayland）
> 关联文档：[MILESTONES.md](./MILESTONES.md)
> 本文只做决策、规格与风险说明，不包含代码，不建仓。

> **归属与维护（2026-11-08 起）**
> 本文是 Palune 的设计基线文档，现随源码仓维护于 `docs/ARCHITECTURE.md`。
> 此前它只存在于工作区外的设计目录（`temp_20261106_03/docs/`），导致 §5.10
> 标识与更新地址的修订无法随 commit 交付；自本次起，任何架构/标识变更都必须在
> 本仓内改本文并随代码提交。文中的决策编号（D1–D12）与章节号（§0–§12）作为
> 外部引用契约保持稳定，不随实现进度重排。

---

## 0. 结论摘要

| 编号 | 决策 | 选 | 不选 | 一句话理由 |
|---|---|---|---|---|
| D1 | 播放内核 | 进程内 libmpv（render context 出帧） | mpv 子进程 + IPC 作为默认路径 | 进程内才能做像素级 UI 合成、GPU 直出与 HDR 元数据控制；IPC 无法交付帧 |
| D2 | Windows 呈现 | `--wid` + `--vo=gpu-next --gpu-context=d3d11`（mpv 自管交换链）+ 独立透明 UI 覆盖窗 | 只用 Qt 场景内合成 | 这是 1.0 唯一能稳定拿到 HDR10 直通与 `d3d11va` 零拷贝的路径 |
| D3 | macOS / Linux 呈现 | libmpv OpenGL render API 出帧到 Qt 的 FBO | `--wid` 嵌入 / 独立窗口 | macOS 的 `wid`（NSView）会吞输入且叠加层脆弱；Wayland 根本没有 `wid` |
| D4 | UI 框架 | Qt 6.8 LTS + QML（单一栈）+ 薄原生层 | Tauri / Electron / 原生三套壳 | 唯一能同时满足“帧由我们合成 + 三平台一致 + 自绘原生质感”的选项；代价见 §3.4 |
| D5 | 兜底路径 | 保留 IPC **仅**用于 CI、诊断、外部控制；渲染兜底用“独立窗口模式” | 把子进程播放作为用户可见兜底 | 子进程同样解决不了 UI 叠加与 HDR；反而多一层状态同步 |
| D6 | 功能边界 | 3D/VR、在线字幕、蓝光菜单降为 P2 或明确砍掉 | 以“覆盖所有桌面功能”为字面承诺 | 这些能力的投入产出比极低，且部分在现代生态已不可行；媒体库不在砍掉名单内，见 D10 |
| D7 | 许可 | 自建 `-Dgpl=false` 的 libmpv（LGPLv2+）+ Qt 动态链接（LGPLv3） | 发行版预编译包 / 静态链接 Qt | 闭源分发必须可控；预编译包通常带 GPL 或版本漂移 |
| D8 | macOS 分发 | Developer ID + 公证（Hardened Runtime）直发 | 1.0 上 Mac App Store 沙盒版 | 沙盒会阻断任意路径读取与脚本/子进程，功能对齐全毁；先直发，沙盒版另立 |
| D9 | 界面主题 | A/B/C 三套**布局级**主题并存，默认 A；令牌 / 组件 / 布局模式 / 装配四层分离 | 三套 QSS/CSS 覆盖，或最后补一层皮 | 主题差异改变控件结构与视频可用面积，样式覆盖无法表达；分层同时保证切主题不触碰播放层（§7） |
| D10 | 媒体库接入 | 独立“内容源层” + 三适配器覆盖五家（Jellyfin/Emby 家族、极空间/飞牛复用、Plex 独立），海报墙与进度双向同步 | 内置刮削自建媒体库，或只做 DLNA/UPnP 浏览 | 五家都有可用的服务端接口；自刮削识别率不可控且要自维护元数据源；内容源层与播放内核解耦，换适配器不动播放代码（§8） |
| D11 | 范围与质量基线 | P0 收敛为十项 + 五项手感指标；SDR 为基线、HDR 三态可见；不做皮肤系统；字幕 ASS/SRT 双轨 | 以“大而全功能清单”为验收基线；字幕全走 libass；用 HDR 开关冒充支持 | 范围不收敛就交不了期；手感必须可测；跨平台没有位图皮肤的对应物；SRT 需要跟随主题令牌（§11） |
| D12 | 产品命名与标识 | **Palune**；三平台标识统一 `app.palune.player`，公测前冻结；目录/安装包/更新源同源命名 | 先占位后改名，或三平台各用一套标识 | 标识与钥匙串、TCC、公证票据、更新链绑定，对外发布后不可逆（§5.10） |

**四条必须先接受的硬事实**（决定了上面的分平台方案与媒体库策略）：

1. libmpv 的 render API 只有 **OpenGL / D3D11 / Software** 三种后端；D3D11 后端是 mpv 0.42 才加入的新 API（`render_d3d11.h`，client API 2.6），尚缺生产验证。
2. macOS 上 libmpv **没有 Metal 后端**；render API 只能 OpenGL。OpenGL 自 macOS 10.14 起被标记弃用但仍以 4.1 兼容形态存在。
3. **Wayland 不支持 `--wid` 外部窗口嵌入**（`client.h` 明确 `wid` 仅 X11 / win32 / macOS）；Wayland 必须走 render API 并由我们提供 EGL 上下文与 surface。
4. 极空间与飞牛只被证实“对外提供 Emby / Jellyfin 兼容服务”，其内部实现与端点覆盖率**未被证实**。工程上必须按“兼容层”处理：配对时做能力探测，缺项即降级，绝不能假设与官方 Jellyfin/Emby 100% 一致（§8）。

因此本方案不是“一条渲染路径通吃”，而是 **一个呈现抽象 + 分平台最优实现**。这会在 §2.4 展开。

---

## 1. 目标、边界与验收基线

### 1.1 功能范围的定义：以播放任务为中心

我们覆盖的是**我们自己的播放能力集合**，不追求逐项复刻任何商业产品的私有实现：

- 播放核心：硬解、HDR、色彩管理、声道/直通、变速、字幕、章节、书签、播放列表、截图、录制、逐帧、滤镜、网络流。
- 内容源：Jellyfin / Emby / Plex 协议接入（含极空间、飞牛的兼容层），海报墙与跨设备播放进度同步（见 §8）。
- 交互能力：可改键、全局热键、置顶/迷你窗、单实例、拖拽、右键菜单、播放历史与续播。
- **不做**：私有的二进制皮肤格式、闭源解码器私有路径、电视卡/DVB 驱动栈、DRM 流媒体。

### 1.2 界面语言与三主题的定义

共同视觉语言是**单窗口、内容优先、无边框 chrome、克制动效**，它不是唯一布局。1.0 同时提供三套**布局级**主题，默认 A；差异见 §6.2，机制见 §7：

- **共同语言**：单窗口、内容优先、无边框 chrome、克制动效（180ms）、键盘优先、所有 P0 操作在三套主题下都可达。
- **A（默认，浮层式）**：浮层 chrome，视频区占满窗口；底部 OSC 自动隐藏；播放列表按需滑出，滑出不改变视频布局。
- **B（停靠式）**：右侧检查器常驻、底部 dock 不覆盖视频；播放/跳转/音量/速度合并为一个 transport 组件；视频可用面积 = 窗口 − 顶 − 底 − 右。
- **C（沉浸式）**：顶部细进度条 + 悬浮胶囊控件，几乎不占画面；播放列表以全屏 overlay 呈现，视频可用面积最大。

三者的差异不止颜色与圆角，而是**组件结构、控件分组、进度条形态、画面可用面积**。因此“主题”在本方案里是一等公民，不是换皮；本文其余章节（快捷键、平台差异、性能纪律）都按三主题并存的前提书写。

### 1.3 质量基线（后续里程碑的验收锚点）

| 指标 | 1.0 目标 | 测量方式 |
|---|---|---|
| 4K60 HEVC HDR10 播放 | 掉帧 ≤ 0.01%，音频 glitch 0 | `frame-drop-count` + 音频欠载日志，2h |
| 启动到首帧（本地 1080p） | ≤ 1.5s（1.0 门槛）；内部目标 ≤ 800ms | 打点日志（进程启动 → 首帧呈现） |
| 本地 seek 精度 | 帧精确；p95 延迟 ≤ 200ms（1080p） | 自动化 seek 脚本 |
| 8h 连续播放内存漂移 | ≤ ±10%，无崩溃 | 进程 RSS 采样 |
| UI 帧率 | 播放中稳定 60fps，UI 帧时间 p95 ≤ 16.7ms | Qt frame stats |
| 崩溃恢复 | 异常退出后重启恢复到 ±1s 位置 | 强杀进程测试 |
| 8K | **不承诺**，仅能力探测与提示 | 支持矩阵记录，不作为保证 |
| 支持媒体矩阵 | 60 个样本 × 3 OS，失败率 < 0.5% 且有据可查 | 见 MILESTONES M3 |

---

## 2. 渲染嵌入方案（问题 1）

### 2.1 四种途径的机制

| 途径 | 机制 | 谁拥有窗口/交换链 | UI 叠加能力 | 输入 | 崩溃隔离 |
|---|---|---|---|---|---|
| A. 独立渲染窗口 + 输入转发 | mpv 自建窗口，应用把键鼠事件转发给它 | mpv | 几乎为零（只能另开覆盖窗） | 需转发，焦点/光标易失控 | 好（可独立进程） |
| B. `wid` 父化 | 传入原生句柄，mpv 创建子窗口并自管交换链 | mpv | 差（Windows 上外来子 HWND 有 airspace 问题；macOS 子视图会吞事件） | mpv 子窗口会抢焦点 | 中 |
| C. Render API（GPU 纹理 / FBO） | `mpv_render_context_create` + `mpv_render_context_render` 到应用提供的 FBO/纹理 | 应用 | 好（与 UI 同一合成链） | 应用完全掌控 | 差（同进程） |
| D. Render API（Software） | 出帧到内存（`MPV_RENDER_PARAM_SW_*`） | 应用 | 好 | 应用 | 差 |

关键补充：

- C 不等于“只支持 OpenGL”。当前有三个后端：`MPV_RENDER_API_TYPE_OPENGL`、`MPV_RENDER_API_TYPE_D3D11`（mpv 0.42+，`MPV_RENDER_PARAM_D3D11_FBO` 传 `ID3D11Texture2D`）、`MPV_RENDER_API_TYPE_SW`。
- C 的渲染必须在持有上下文的线程调用；OpenGL 要求上下文在该线程 current，且必须是创建 render context 的同一个上下文。渲染线程不应阻塞等待 mpv core，否则会触发 “`mpv_render_context_render() not being called or stuck`”，开 `MPV_RENDER_PARAM_ADVANCED_CONTROL` 时违规会真死锁。
- B 的句柄类型：X11 为 `Window`，Windows 为 `HWND`（按 `uint32_t` 传，0 有特殊含义），macOS 为 `NSView*`（`client.h` 明确支持，但 manpage 未列 macOS）；Wayland 不支持。

### 2.2 三平台可行性矩阵

| 途径 | Windows | macOS | Linux X11 | Linux Wayland |
|---|---|---|---|---|
| A 独立窗口 | 可行，但不可接受（无叠加、双窗口） | 可行，但不可接受 | 可行，但不可接受 | 可行（mpv 自建 surface），但不可接受 |
| B `wid` | **可行**（HWND；mpv.net 已生产验证） | **可行**（NSView*），但输入/叠加差 | **可行**（mpv 建子窗口盖满父窗口） | **不可行**（无外部窗口嵌入） |
| C OpenGL | 可行（WGL/ANGLE；`--gpu-context=win/angle`） | 可行（`NSOpenGLContext`/`CAOpenGLLayer`；GL 4.1 已弃用） | 可行（GLX/EGL） | 可行（EGL + 自建 `wl_surface`；要自己处理 frame callback/缩放/tearing） |
| C D3D11 | 可行（mpv 0.42+；应用自建 DXGI 交换链） | 不可用 | 不可用 | 不可用 |
| D Software | 可行（诊断/CI） | 可行（诊断） | 可行（诊断） | 可行（诊断） |

### 2.3 已知坑（按平台）

#### Windows

- **DWM / airspace**：`--wid` 下 mpv 的 video 是外来子 HWND，Qt/WPF 控件画在同一父窗口上永远被它盖住。所以控制层（A 的 OSC、B 的 dock、C 的胶囊）不能放在同一窗口，必须用**独立透明覆盖窗**（另见 §2.4）。覆盖窗只承载静态 UI，DWM 会缓存，不会每帧合成一层全屏 alpha。
- **高 DPI**：进程必须 `PER_MONITOR_AWARE_V2`；Qt 侧 `HighDpiScaleFactorRoundingPolicy=PassThrough`；跨屏拖动时处理 `WM_DPICHANGED`，交换链按物理像素重建。`--hidpi-window-scale` 只对 mpv 自建窗口有效，`wid` 模式下窗口尺寸由我们负责。
- **HDR**：Windows 需要 DXGI flip-model 交换链并 `SetColorSpace1`；mpv 只有在 **自管交换链** 时才能把颜色空间设对（`--d3d11-output-csp`、`--target-colorspace-hint` 在 D3D11/winvk 上下文可用）。`target-colorspace-hint` 官方支持范围是 **Wayland / D3D11 / winvk**，不含 macOS。`winvk` 虽在支持列表内，但实际 HDR 表现依驱动/显示器组合而定；无实测证据前不做默认。
- **MPO / 分层窗口**：视频面不要加 `WS_EX_LAYERED`；覆盖窗用 `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`，空区域用 `WM_NCHITTEST` 返回 `HTTRANSPARENT` 让点击穿透，避免“全屏透明窗吃事件”。某些驱动/多平面叠加下会出现闪烁或黑闪，需在 M0 用 Intel/NVIDIA/AMD 三卡复现。
- **混合显卡（Optimus/Hybrid）**：d3d11va 可能落在错误适配器上；用适配器 LUID 固定设备，并在启动时记录 GPU 厂商/驱动版本。
- **独占全屏/VRR**：不要在 1.0 做独占全屏；一律无边框窗口 + DWM。VRR 由系统控制，不做强制。
- `--wid` 传 0 会画在桌面壁纸上（有被系统销毁窗口的已知问题），绝不使用。

#### macOS

- **OpenGL 弃用**：libmpv 无 Metal 后端，render API 只能 GL。若 Qt Quick 用 Metal RHI，就无法与 GL 纹理合成；因此 macOS 构建必须强制 `QSG_RHI_BACKEND=opengl`，整个 UI 也跑在 GL 上。这不是优雅的选择，但比“Qt 渲染一层 + 视频另一层 + 事件转发”稳定。
- **`wid`（NSView）的问题**：mpv 会在给定 view 下自建子视图，键鼠事件先落到它；想在同一窗口叠 UI 需要再加一个覆盖子视图，而 layer-backed 视图叠在 `CAOpenGLLayer` 上的合成在部分 macOS 版本会出现闪烁/排序异常。故 macOS 默认不用 `wid`。
- **EDR / HDR**：`--target-colorspace-hint` 在 macOS 不可用；mpv 自己的 cocoa-cb 路径有 `--cocoa-cb-output-csp` 与 `--cocoa-cb-10bit-context`（可激活 EDR），但这套只对 mpv 自建窗口生效。走 render API 时是我们自己持有 layer，需要自行设置 `contentsFormat`、`wantsExtendedDynamicRangeContent` 与 PQ/HLG 色彩空间，并让 mpv 输出 PQ（`--target-trc=pq --target-prim=bt.2020`）到 FP16 FBO。**这是 M0 的必做 spike**，失败就降级为 SDR tone-mapping 或启用 `wid`+cocoa-cb 的 HDR 模式。
- **公证与沙盒**：直发版需 Hardened Runtime、Developer ID 签名所有内嵌 dylib/framework、公证与 stapling；沙盒版无法任意读文件、无法启动外部 helper（yt-dlp/ffmpeg），功能裁剪严重。`com.apple.security.cs.disable-library-validation` 只在确有第三方插件加载需求时开启，且需写进风险清单。
- **显示同步**：`CVDisplayLink` 在 macOS 14 起弃用，改 `CADisplayLink`（macOS 14+）或 `NSView.displayLink`；Apple Silicon 上不要假设固定 60Hz，按实际刷新率（含 ProMotion 自适应）计算帧间隔。
- **Apple Silicon GL**：可用且有硬件驱动，但性能调优空间小于 Metal；4K60 + 复杂着色器要单独压测。

#### Linux

- **Wayland 无 `wid`**：必须 render API + 自建 `wl_surface`/subsurface、EGL 上下文（`EGL_EXT_platform_wayland`）、处理 `wl_surface.frame` 回调、`buffer_scale`、分数缩放、以及 tearing-control（`wp_tearing_control_v1`，若需要关闭 vsync）。合成器差异大（Mutter/KWin/wlroots/sway），必须在 M0 覆盖至少 3 个合成器。
- **X11**：`wid` 可用，但 compositor 下 vsync 依赖 presentation 扩展；`_NET_WM_BYPASS_COMPOSITOR` 可减少一层合成，但会破坏叠加窗。1.0 保持合成器开启。
- **GPU 差异**：Intel/AMD 走 `vaapi`（无 dmabuf modifiers 时需要 EGL），NVIDIA 走 `nvdec`（专有驱动 + Wayland 的 EGL/dmabuf 组合历史上坑多）。`hwdec=auto-safe` 不够，必须做厂商级策略表。
- **HDR**：依赖 Wayland color-management 协议与合成器支持（KDE 6.x 较新版本可用，GNOME 尚不完整）；1.0 不承诺，统一 tone-map，P2 再开。
- **音频**：PipeWire 优先，Pulse 兼容；`--audio-exclusive` 在 PipeWire/WASAPI/CoreAudio/AudioUnit 有效，其他 AO 会静默忽略。
- **打包**：AppImage（直发）、Flatpak（沙盒、portal 文件访问、GPU 设备权限）、Snap（严格 confinement）。三种都要测，否则“Linux 支持”是空话。

### 2.4 推荐组合（最终决策）

**呈现抽象**：`IVideoPresenter` 四个实现，按平台与模式选择；上层 UI/播放逻辑不感知差异。

| 平台 | 主路径（默认） | 关键配置 | 备用路径 | 说明 |
|---|---|---|---|---|
| Windows | `wid` + D3D11 + 独立透明 UI 覆盖窗 | `--vo=gpu-next --gpu-context=d3d11 --hwdec=d3d11va --d3d11-flip=yes --target-colorspace-hint=auto --d3d11-output-csp=auto`（HDR 屏时 `pq`） | GL render API + Qt FBO（无 HDR） | 选它是因为 **HDR 与零拷贝只能在 mpv 自管交换链时成立**；代价是覆盖窗与双窗口同步 |
| macOS | OpenGL render API → Qt FBO | `--vo=libmpv --hwdec=videotoolbox`；EDR 时 `--target-trc=pq --target-prim=bt.2020` + 自建 layer 色彩空间 | `wid`(NSView) + cocoa-cb（HDR 兜底）；SW（诊断） | 选它是因为 macOS 上 `wid` 的输入/叠加问题无解，而 GL render API 是唯一能同窗口合成的路径 |
| Linux X11 | OpenGL render API → Qt FBO | `--vo=libmpv --hwdec=vaapi/nvdec` | `wid`（X11 调试用） | 统一与 Wayland 一致的路径，减少分支 |
| Linux Wayland | OpenGL render API + EGL | 同上；`--hwdec=vaapi`（dmabuf）| SW（诊断） | `wid` 不可用，别无选择 |

**为什么不把 Windows 也统一到 render API：** 因为 Qt 的 D3D11 场景合成不暴露交换链色彩空间，HDR 无法通过 Qt 的 swapchain 输出；而 D3D11 render API 要求我们自己建交换链，0.42 又太新。用 `wid` 让 mpv 管交换链，再把 UI 放在独立覆盖窗，是当前风险最低且 HDR 确定可用的组合。等 0.42 的 D3D11 render API 经过 2–3 个版本验证后，可在 M7 评估合并为单一路径（届时用 `QQuickRhiItem` + `QRhiTexture` → `MPV_RENDER_PARAM_D3D11_FBO`）。

**Windows 覆盖窗的工程约束（必须写进实现规格）：**

- 视频窗：无边框、任务栏隐藏（`WS_EX_TOOLWINDOW`）、不抢焦点、不接收输入（输入全由覆盖窗处理）。
- 覆盖窗：无边框、透明、`WS_EX_NOACTIVATE`、始终位于视频窗之上；仅在控件区域命中，其余 `HTTRANSPARENT`。
- 两窗同步：移动/缩放时先改视频窗再改覆盖窗，同一帧内完成；全屏用同一组窗口标志；多显示器 DPI 变化同时重算两窗。
- 任务栏/Alt-Tab 只显示覆盖窗（它是“应用窗口”），视频窗必须是工具窗。
- 截图/录屏工具看到的是两窗合成结果，正常。

### 2.5 色彩与 HDR 管线（三平台差异必须显式建模）

| 环节 | Windows（wid/D3D11） | macOS（GL render API） | Linux（GL render API） |
|---|---|---|---|
| 输出色彩空间设置 | mpv 自管交换链，`target-colorspace-hint` + `--d3d11-output-csp` | 应用设置 layer 的 CGColorSpace + EDR；mpv 只负责输出 PQ 值 | 依赖合成器；1.0 统一 tone-map 到 sRGB |
| HDR10/HLG 直通 | 支持（HDR10；Vulkan 路径不支持） | 实验性（EDR spike 决定） | P2（等 color-management 协议普及） |
| tone mapping | libplacebo（gpu-next） | libplacebo | libplacebo |
| 系统 ICC | `--icc-profile-auto` 可用 | 需要应用读 ColorSync 后经 `MPV_RENDER_PARAM_ICC_PROFILE` 传入 | X11 可用，Wayland 取决于合成器 |
| 截图 | `screenshot` 命令，HDR 保留色彩标记（`screenshot-tag-colorspace`） | 同左；`window` 模式需自行合成 UI | 同左 |

结论（§11.5 为正式约束）：

- **SDR 是 1.0 的质量基线**：Rec.709 / sRGB，tone mapping 后 ΔE2000 ≤ 3；任何平台、任何 GPU 都必须正确，HDR 不能成为牺牲 SDR 的理由。
- HDR **只有三态且必须可见**：`HDR 直通` / `HDR 已检测，映射到 SDR` / `SDR`。状态由实测能力探测决定，不由用户开关决定；设置页与播放信息面板同时展示。
- **禁止**用错误的 tone mapping、假 PQ 标记或“HDR 开关”冒充支持；探测失败一律落到映射态或 SDR 态，并明确告知用户当前不是 HDR。
- **8K 明确不承诺**：只做能力探测与提示，不进入验收与对外保证（§11.13）。

### 2.6 是否需要保留 mpv 子进程 + IPC 兜底

**保留，但不作为用户可见的播放兜底。** 具体划分：

| 用途 | 是否保留 | 理由 |
|---|---|---|
| CI / headless 回归（`--vo=null --ao=null`） | 保留 | 无需 GPU 与窗口，跑 60 文件矩阵最快 |
| 外部控制 / 调试（脚本、自动化测试、MPRIS 适配） | 保留 | `--input-ipc-server` + JSON IPC 成本低 |
| 解码异常隔离（某个文件崩溃） | 不保留为运行时兜底 | 子进程也必须用 `wid` 或独立窗口，UI 叠加/HDR 问题原样存在；且帧无法回传 |
| 用户可见的“兼容模式” | 不做 | 双状态同步、截图/字幕样式不一致，维护成本高于收益 |

IPC 规格（如果启用诊断模式）：Windows 命名管道 `\\.\pipe\<app>-<pid>`，macOS/Linux Unix socket（`$XDG_RUNTIME_DIR` 或 `~/Library/Application Support/<app>/ipc.sock`）；协议为逐行 JSON（`{"command":[...]}` / `{"event":...}`）；只允许 `loadfile/seek/pause/set_property` 等白名单命令；本机往返延迟目标 < 5ms。**它不参与正常播放路径。**

若 GPU 渲染彻底失败（驱动/上下文创建失败），真正的兜底是 **Presenter 降级链**：D3D11 → GL → SW → 独立窗口提示，而不是子进程。

---

## 3. UI 框架选型（问题 2）

### 3.1 评估维度与结果

评分 1–5，5 最好。“渲染配合度”指能否把 libmpv 的帧放进自己的合成链并叠加 UI。

| 维度 | Qt 6 + QML | Tauri + Web | Electron | 原生三套壳 |
|---|---|---|---|---|
| 渲染配合度 | 5（FBO/RHI 纹理可直接交给 mpv） | 2（WebView 不是可编程 GL 表面） | 1（只能拷贝帧或转码推流） | 5 |
| 包体积（trim 后） | 约 45–70 MB | 约 8–15 MB + 系统 WebView | 约 150–250 MB | 约 25–45 MB |
| 开发效率 | 4（QML 快，C++ 集成有学习曲线） | 4（Web 前端快，原生胶水慢） | 5（Web 最快） | 2（三套 UI 三套人力） |
| 跨平台一致性 | 4 | 4 | 5 | 2 |
| 原生质感（自绘 + 系统材质） | 4（需自绘 + 薄原生层） | 3（排版易，材质/窗口手感难） | 3 | 5 |
| 视频专用能力（HDR/色彩/多窗） | 5 | 2 | 1 | 5 |
| 许可与分发 | LGPLv3 动态链接可闭源 | MIT/Apache 宽松 | MIT 宽松 | 自有 |
| 长期维护成本 | 3 | 3（三平台 WebView 差异） | 4 | 2 |

### 3.2 逐项否决理由

- **Tauri + Web**：致命点在“视频表面”。macOS 的 WKWebView 是 layer-backed，无法把 libmpv 的 GL 帧作为可合成子层交给它；Windows WebView2 是子 HWND，透明背景与 z-order 组合脆弱；Linux WebKitGTK 同理。可行的只剩“每帧拷贝到 canvas/WebGL”或“转码成 WebRTC/MediaStream”，前者 4K60 的带宽与延迟不可接受、且 HDR/色彩管理直接丢失，后者引入编码延迟与画质损失。**否决，不是因为它慢，而是因为它拿不到帧。**
- **Electron**：同上，且更重。`BrowserWindow` 无法承载原生视频面；离屏渲染再上传的成本更高。**否决。**
- **原生三套壳（SwiftUI/AppKit + WinUI 3 + GTK4）**：质量上限最高，Windows 还能用 `--d3d11-output-mode=composition` 拿到 `display-swapchain` 并挂到 `SwapChainPanel`，HDR 与 XAML 叠加都很干净。但这是三套 UI、三套输入/无障碍/主题/更新/打包，1.0 不可能。**否决为 1.0 方案，保留为 macOS 应急路线**（见 §3.5）。
- **Qt + QML**：唯一同时满足“我们能拿到帧 + 一个 UI 栈 + 可接受的质感”的方案。代价明确写在 §3.4。

### 3.3 推荐：Qt 6.8 LTS + QML

- 版本：Qt 6.8 LTS（长期支持），Widgets 仅用于极少数对话框，主界面全部 QML。
- 集成方式：
  - macOS / Linux：`QQuickFramebufferObject` + `Renderer::render()` 内调用 `mpv_render_context_render`，FBO 由 Qt 提供，UI 在场景图上层合成。
  - Windows：`wid` 视频窗 + 独立 QML 覆盖窗；QML 侧可用 D3D11 RHI（Qt 默认），无需强制 GL。
  - 未来（M7）：Windows 切到 D3D11 render API + `QQuickRhiItem`，与 macOS/Linux 统一为“场景内合成”。
- 窗口层：Obj-C++（macOS 交通灯/vibrancy/EDR layer）、Win32（覆盖窗、DWM、DPI）、X11/Wayland（CSD、surface 回调）各一层薄封装，QML 不直接接触平台 API。
- 主题：采用 §7 的“令牌 + 组件 + 布局模式 + 装配”分层；QML 的 `Loader` 只切 `LayoutMode`，不切整棵 UI，也不使用 QSS 覆盖。

### 3.4 Qt 方案的代价（必须承认）

1. **macOS 全栈 GL**：为了与 libmpv 合成，Qt Quick 必须强制 OpenGL RHI；UI 无法用 Metal，功耗与未来兼容性都背着 Apple 的弃用风险。缓解：把 GL 相关代码收敛在 `Presenter`，M7 评估原生 macOS 壳或 mpv Metal 后端进展。
2. **原生质感要自己做**：QML 没有原生 vibrancy、没有 traffic lights、没有系统级毛玻璃。需要 Obj-C++ 注入 `NSVisualEffectView`、无边框窗拖拽、圆角裁剪，Linux 还要处理无服务端装饰。
3. **LGPLv3 合规**：Qt 必须动态链接，随包提供许可证与源码获取方式，允许用户替换 Qt 库；不能静态链接（除非买商业许可）。**libmpv 同理，动态链接是硬约束，禁止静态链接**（§11.7）。
4. **QML 性能纪律**：视频上方不要放全屏模糊/阴影；控制层素材缓存为位图；每帧只更新必要文本（时间码 4Hz，不是 60Hz）。
5. **Windows 双窗口**：覆盖窗的焦点、任务栏、全屏、DPI、Alt-Tab 都要专门处理，这是 Windows 独有的复杂度。

### 3.5 何时切换到“macOS 原生壳”

触发条件（任一成立即启动 B 方案评估）：

- Apple 在 WWDC 宣布移除/停止签署 OpenGL 应用，或实测目标 macOS 版本 GL 性能不达标。
- macOS EDR 自建 layer 的 spike 失败，且 `wid`+cocoa-cb 的双窗口方案也无法满足 HDR 验收。
- macOS 端 UI 质感评审连续两轮不达标。

B 方案形态：共享 C++ core（播放/状态/快捷键/持久化），macOS 用 AppKit + Metal 自绘 UI + libmpv GL render API（或未来的 Metal 后端）。代价：多一套 UI，约 +30% 前端工作量。

---

## 4. 功能清单与难度分级（问题 3）

难度：**低** = 接线与 UI 包装；**中** = 需自研状态机/平台适配；**高** = 渲染/驱动深水区；**极高** = 不建议在 1.x 做。
优先级：P0 = 1.0 必须有；P1 = 1.0 应有或 1.1；P2 = 后续/可选。

**优先级口径（评审采纳，§11.1）**：P0 收敛为十项——播放、字幕、音轨、硬解、变速、截图、快捷键、记忆位置、下一集、全屏。下表任何标 P0 但不在十项内的能力（章节、播放列表、崩溃恢复等）按 P1 处理；标 P1 但服务十项的（如字幕样式）按 P0 处理。完整对照见 §4.8。

### 4.1 播放核心

| 能力 | libmpv 支持 | 实现路径 / 关键选项 | 难度 | 优先级 |
|---|---|---|---|---|
| H.264/HEVC/VP9/AV1 硬解 | 原生 | `--hwdec=auto-safe`；Win `d3d11va`，mac `videotoolbox`，Linux `vaapi`/`nvdec` | 低 | P0 |
| 10/12-bit 与 HDR 源解码 | 原生 | 同上；AV1 用 dav1d/硬解 | 低 | P0 |
| 硬解失败自动回退 | 原生 | `--hwdec-software-fallback`；应用层再加失败计数与提示 | 低 | P0 |
| 零拷贝直出 | 部分 | `MPV_RENDER_PARAM_ADVANCED_CONTROL` + `--vd-lavc-dr`；依赖后端与驱动 | 中 | P1 |
| HDR10/HLG 直通 | 部分 | Win：wid+D3D11+`target-colorspace-hint`；mac：EDR layer；Linux：P2 | 高 | P1 |
| HDR→SDR tone mapping | 原生 | `--vo=gpu-next --tone-mapping=...`（libplacebo） | 低 | P0 |
| ICC / 3D LUT | 原生 | `--icc-profile`、`--icc-3dlut-size`、`--icc-profile-auto`；mac 自行传 `MPV_RENDER_PARAM_ICC_PROFILE` | 低 | P1 |
| 亮度/对比/饱和/色相/Gamma | 原生 | 对应 property 直通 | 低 | P1 |
| 色彩空间/码率/解码器信息 | 部分 | 读 `video-params`、`current-vo`、`decoder` 等属性，自绘面板 | 低 | P1 |
| Dolby Vision | 部分 | gpu-next 对 P5/P8 有支持，P7 不完整；不做 DV 元数据直通 | 中 | P2 |
| 8K / AV1 | 部分 | 解码原生，性能取决于 GPU；做能力探测与降级建议 | 中 | P2 |

### 4.2 音频

| 能力 | libmpv 支持 | 实现路径 / 关键选项 | 难度 | 优先级 |
|---|---|---|---|---|
| 输出设备选择 / 独占 | 原生 | `--audio-device`、`--audio-exclusive`（wasapi/coreaudio/pipewire/audiounit 有效） | 低 | P0 |
| 声道布局 / 下混 | 原生 | `--audio-channels=7.1,5.1,stereo`；HDMI 场景强制白名单 | 低 | P0 |
| 音量/增益/音频延迟 | 原生 | `volume`、`volume-gain`、`audio-delay` | 低 | P0 |
| 压缩音频直通（AC3/EAC3/DTS/TrueHD） | 原生但平台受限 | `--audio-spdif=ac3,eac3,dts,dts-hd,truehd`；macOS CoreAudio 直通受限，需自研 AudioUnit/HDMI 路径 | 中 | P1 |
| 变速不变调 | 原生 | `--speed` + `--audio-pitch-correction`（scaletempo2，可选 rubberband） | 低 | P0 |
| 音频滤镜 / 均衡器 | 部分 | `--af=lavfi`；EQ 预设需自建 UI | 中 | P2 |
| 音频可视化 | 极少 | 需自取 PCM（af 输出或 ao tap）再送 GPU；成本高 | 高 | P2 |

### 4.3 字幕

| 能力 | libmpv 支持 | 实现路径 / 关键选项 | 难度 | 优先级 |
|---|---|---|---|---|
| SRT/ASS/SSA/VobSub/PGS | 原生 | libass；`--sub-auto=fuzzy`、`--sub-file` | 低 | P0 |
| 字幕样式（字体/描边/阴影/边距） | 原生 | `--sub-font*`、`--sub-ass-override=strip/scale/force`、`--sub-margin-*`（render API 下字幕由 mpv 渲染进视频帧） | 低 | P0 |
| 双字幕 | 原生 | `--secondary-sid`（toptitle；格式化标签被禁用） | 低 | P1 |
| 编码识别 / 乱码修复 | 原生 | `--sub-codepage=auto`（uchardet） | 低 | P1 |
| 拖拽外挂字幕 / 字幕搜索 | 部分 | 拖拽自建；在线搜索需外部服务（合规/密钥） | 中 / 高 | P1 / P2 |
| 字幕编辑器 / 时间轴微调 | 极少 | 自建编辑器 + `sub-delay`；不能直接改 libass 渲染 | 中 | P2 |
| 在线翻译 | 无 | 外部 API；隐私与合规风险 | 高 | P2 |

#### 4.3.1 字幕双轨渲染（§11.4）

| 类别 | 归属 | 做什么 | 不做什么 |
|---|---|---|---|
| ASS / SSA | libass（mpv 内） | 传 `--sub-ass-override`、字体与边距；保留特效、定位、卡拉 OK、多图层 | 不解析脚本、不重排、不替换图层 |
| SRT | 应用侧自绘 | 解析、断行、双向文本、描边/阴影、安全区、字号档；以 `time-pos` 驱动 | 不经 libass 转 ASS，不烧进视频帧 |
| 共用 | 应用侧 | 字体发现与加载、样式令牌（字号/颜色/描边/安全区）、时序换算（`video-params` OSD 尺寸与像素比）、`sub-delay`、轨切换 | 两轨不得互相覆盖样式 |

混排规则：双字幕时 ASS 在下、SRT 在上。截图规则：`window` 模式必须合成自绘字幕，纯视频截图不得含字幕。**不做“全走 libass”**，理由见 §11.4。

### 4.4 播放控制与组织

| 能力 | libmpv 支持 | 实现路径 / 关键选项 | 难度 | 优先级 |
|---|---|---|---|---|
| 帧精确 seek / 逐帧 | 原生 | `--hr-seek=yes`、`frame-step`、`frame-back-step`（back-step 在部分 codec/hwdec 下慢或不可用，需降级提示） | 低–中 | P0 |
| 章节 | 原生 | `chapter-list`、`--chapters-file` | 低 | P1 |
| A-B 循环 | 原生 | `ab-loop-a/b` | 低 | P1 |
| 书签 | 无 | 自建（SQLite 持久化 + seek） | 中 | P2 |
| 播放列表 / 队列 / 续播 | 部分 | `playlist-*` 命令；队列、历史、续播自建 | 中 | P1（续播属 P0 记忆位置） |
| 记忆位置（位置/音量/字幕轨/窗口几何/倍速） | 应用侧 | `state.db` + `[ui.window]`；键规则见 §11.8；指标见 §11.2 | 中 | P0 |
| 下一集 / 自动连播 | 应用侧 | 最小队列 + `playlist-next`；剧集按 season/episode 排序，播完自动切 | 低–中 | P0 |
| 音轨/字幕轨切换、外挂音轨 | 原生 | `aid`/`sid`、`--audio-file-auto` | 低 | P0 |
| 全屏 / 无边框全屏 | 平台窗口 API | 见 §11.10 窗口状态；三平台各自实现，禁止独占全屏 | 低 | P0 |
| 反向播放 | 原生但实验性 | `--play-direction=backward`；`stream-record` 会失效、性能差 | 中 | P2 |
| 无缝衔接 / gapless | 部分 | 依赖格式与 demuxer | 中 | P2 |

### 4.5 画面与输出

| 能力 | libmpv 支持 | 实现路径 / 关键选项 | 难度 | 优先级 |
|---|---|---|---|---|
| 滤镜链（去隔行/降噪/锐化/裁剪/旋转） | 原生 | `--vf`/`--af` + lavfi；预设自建 | 中 | P1 |
| 自定义着色器（Anime4K 等） | 原生 | `--glsl-shaders`（gpu-next 下需验证 shader 兼容） | 低–中 | P1 |
| 截图（含字幕/HDR/16bit） | 原生 | `screenshot` 命令、`--screenshot-high-bit-depth`、`--screenshot-tag-colorspace`；render API 下 `window` 模式需自行合成 UI | 低–中 | P0 |
| 录制（流复制） | 原生 | `--stream-record`；seek 会断点，需在 UI 明示 | 中 | P1 |
| 分段录制 / 转码录制 | 无 | 外挂 ffmpeg 子进程（NVENC/QSV/AMF/VideoToolbox） | 中–高 | P1（Win 优先） |
| 3D（SBS/OU → 红蓝/交错） | 部分 | lavfi `stereo3d` 近似；mpv 原生 stereo 模式已移除 | 中 | P2 |
| 360/VR | 无 | 需自研 shader 与头显支持 | 高 | 砍掉 |
| 逐帧导出 / GIF | 无 | ffmpeg 子进程 + 任务队列 | 中 | P2 |

### 4.6 网络与外部内容

| 能力 | libmpv 支持 | 实现路径 / 关键选项 | 难度 | 优先级 |
|---|---|---|---|---|
| HTTP/HLS/DASH/RTSP/RTMP | 原生 | ffmpeg 协议栈；DRM 流不支持 | 低–中 | P0/P1 |
| SMB/FTP/WebDAV | 原生（编译期） | FFmpeg 协议；需在自建包里启用 | 低–中 | P1 |
| 代理 / UA / 自定义请求头 | 原生 | `--http-proxy`、`--user-agent`、`--http-header-fields` | 低 | P1 |
| yt-dlp 站点解析 | 原生 hook | `--ytdl=yes`；需打包 yt-dlp 并提供更新机制 | 低（工程外） | P1 |
| 直播源 / EPG | 无 | 自建订阅、解析、定时刷新 | 高 | P2 |
| 蓝光原盘 | 部分 | libbluray 可播主正片，无 BD-J 菜单 | 高 | P2 |
| DRM 流媒体（Netflix 等） | 不支持 | 明确不做 | — | 不做 |

### 4.7 应用层能力

| 能力 | 实现路径 | 难度 | 优先级 |
|---|---|---|---|
| 控制层（OSC / dock / 胶囊）与悬浮提示 | QML 共享组件，由布局决定形态 | 中 | P0 |
| 快捷键 / 全局热键 | 自建 dispatcher；按平台分列绑定表 + 启动冲突检测（§11.9） | 中 | P0 |
| A/B/C 三套布局主题 + 运行时切换 | 令牌/组件/布局模式/装配四层（§7） | 中–高 | P0 |
| 置顶 / 迷你窗 / 画中画 | 原生窗口 API + 状态机 | 低–中 | P1 |
| 单实例 / 文件关联 / 协议 | 平台 API + 本地 IPC | 低–中 | P1 |
| 缩略图 / 进度条预览 | ffmpeg 抽帧 + 雪碧图缓存 | 中 | P1 |
| 媒体服务器接入：海报墙 + 进度同步 | 内容源层 + 三适配器覆盖五家（§8） | 高 | P1 |
| 媒体服务器能力探测与降级 | 配对时 `probe()`，缺项隐藏或退化到文件通道（§8.2） | 中 | P1 |
| 自动更新 | WinSparkle / Sparkle / AppImage 自更新 | 中 | P1 |
| 崩溃恢复 / 断点续播 | 见 §5.8 | 中 | P1（记忆位置本身是 P0） |
| 诊断包导出 / 日志 | 自建 | 低 | P1 |
| i18n / 无障碍 | Qt Linguist + 焦点/读屏规范 | 中 | P1 |

**明确砍掉 / 降级（要在产品上写清）**：VR 头显、3D（SBS/OU 转红蓝）、私有二进制皮肤格式与位图皮肤、第三方皮肤系统、BD-J 菜单、DRM 流、Windows 电视卡、软解滤镜矩阵。理由见 §11.1、§11.3。

### 4.8 P0 十项对照与排除清单（§11.1）

| # | P0 项 | 对应能力（本表条目） | 1.0 验收口径 |
|---|---|---|---|
| 1 | 播放 | 打开/播放/暂停/停止、seek、逐帧、播放队列最小集 | 60 样本 × 3 OS 失败率 < 0.5% |
| 2 | 字幕 | ASS（libass）+ SRT（自绘）、轨切换、延迟、样式档（§4.3.1） | 复杂 ASS 与 SRT 各 10 例，位置/字号/安全区正确 |
| 3 | 音轨 | `aid` 切换、外挂音轨、音量/静音 | 多音轨样本全 PASS，切换无爆音 |
| 4 | 硬解 | `hwdec=auto-safe` + 失败回退 | 注入错误 hwdec 100% 回退且提示一次 |
| 5 | 变速 | `--speed` + pitch correction，0.25×–4× | 变速后音画同步 ≤ 50ms，无变调 |
| 6 | 截图 | 原画/含字幕/HDR 标记 | 纯视频不含自绘字幕；window 模式含；ΔE ≤ 3 |
| 7 | 快捷键 | 分平台绑定 + 冲突检测 | 40 个动作三平台可绑定，冲突项可定位（§11.9） |
| 8 | 记忆位置 | 位置/音量/字幕轨/倍速/窗口几何 | 强杀 100 次恢复 ±1s，成功率 100%（§11.2） |
| 9 | 下一集 | 剧集排序 + 播完自动切 | 连续 5 集自动播放，间隔 ≤ 2s，进度正确写入 |
| 10 | 全屏 | 无边框全屏 + 全屏隐藏鼠标 | 三平台切换 ≤ 1 帧偏移，退出恢复到窗口几何 |

**1.0 明确排除**（不排期、不验收、不对用户承诺）：3D/VR、位图皮肤与第三方皮肤、软解滤镜矩阵、BD-J、DRM 流、电视卡、在线字幕搜索/翻译、内置刮削建库。媒体服务器（§8）为 P1 独立阶段，不计入十项验收。

---

## 5. 工程结构（问题 4）

### 5.1 分层

```
┌──────────────────────────────────────────────┐
│ UI 层（QML）：A/B/C 布局 / 共享组件 / 设置 / 对话框  │
├──────────────────────────────────────────────┤
│ 应用服务层：PlayerFacade / Shortcut / Theme /  │  ← 只依赖 core，不依赖 Qt 细节
│            MediaLibraryService（§8）          │
├──────────────────────────────────────────────┤
│ 播放域（core）：状态机 / 队列 / 历史 / 书签    │  ← 可单元测试，无 UI
├───────────────┬──────────────┬───────────────┤
│ libmpv 适配层  │ 呈现抽象层    │ 持久化层       │
│ (MpvClient)   │ (Presenter)  │ (Config/DB)   │
├───────────────┴──────────────┴───────────────┤
│ 内容源层：库视图 / 进度合并 / Jellyfin·Emby·Plex 适配器（§8） │
├──────────────────────────────────────────────┤
│ 平台层：窗口 / HDR / 音频设备 / 热键 / 媒体键   │
└──────────────────────────────────────────────┘
```

约束：`core` 不包含任何 mpv 头文件与 Qt Widgets；`Presenter` 不感知播放逻辑；平台层不向 UI 暴露裸句柄（只给封装后的能力）。内容源层**不 import mpv/QML**，只产出规范化模型与可播放 URL；播放域与 UI 都**不 import 具体适配器**，只依赖 `MediaLibraryService` 的接口。反向依赖（适配器回调 UI、适配器直接调 `MpvClient`）一律禁止。

### 5.2 进程与线程模型

**单进程**（决策 D1），线程如下：

| 线程 | 职责 | 禁止事项 |
|---|---|---|
| 主线程 | QML、窗口、输入、设置 | 不做阻塞 IO；不直接等待 mpv 命令返回 |
| 渲染线程 | Qt 渲染线程或专用 GL 线程；调用 `mpv_render_context_render` | 不调用非 render 的 libmpv API；不阻塞等待 core |
| 事件泵线程 | 阻塞 `mpv_wait_event`，转成 UI 事件 | 不碰 UI 对象，只投递队列 |
| 命令线程（可选） | `mpv_command_async` 与超时管理 | 不共享 mpv 属性缓存 |
| mpv 内部线程 | demux / 视频解码 / 音频解码与 AO | — |
| 看门狗线程 | 心跳、GPU 设备丢失检测、崩溃面包屑落盘 | 不做重活 |

线程规则（来自 `render.h` 的硬约束）：

- 同时只能有一个 `mpv_render_*` 调用在飞（同一 core）；不同 core 可并行。
- **绝不能在 `mpv_set_wakeup_callback` 或 `mpv_render_context_set_update_callback` 内调用 render 函数**。
- OpenGL 后端：调用线程必须 current 同一上下文；`mpv_render_context_create` 与 render 必须在同一上下文。
- 开 `MPV_RENDER_PARAM_ADVANCED_CONTROL` 就承诺渲染线程绝不等待 core；否则会真死锁。1.0 只在 M0 验证通过后开启。
- 更新回调只做一件事：置原子标志 + `QMetaObject::invokeMethod(window, update, Qt::QueuedConnection)`；合并多次通知，避免每帧多次合成。

### 5.3 libmpv 适配层（MpvClient）

职责边界：

- 生命周期：create → 设置属性（渲染上下文必须早于 VO 创建）→ 加载 → 关闭。
- 命令：统一命令队列，`loadfile`/`seek` 等长耗时命令走 async 并带请求 ID，UI 可取消/去重；连续 seek 合并为最后一次。
- 属性：只观察需要的属性（`time-pos`、`duration`、`pause`、`core-idle`、`eof-reached`、`track-list`、`chapter-list`、`video-params`、`current-vo`、`decoder`、`demuxer-cache-state`、`frame-drop-count`、`mistimed-frame-count`），按频率分档（时间码 4Hz、状态事件驱动、统计 1Hz）。
- 错误映射：mpv 的错误码/日志统一映射为 `PlayerError{code, file, stage, recoverable}`，驱动 UI 的错误页与自动回退。
- 选项策略：内置一组“安全基线”选项；用户可覆盖白名单；`--script`、`--input-ipc-server`、`--config` 等危险项不允许从 UI 直接写入。
- 版本适配：所有选项名/命令名集中在 `MpvCompat` 表，启动时自检，缺失即降级并记录。

### 5.4 IPC 边界（仅诊断/控制模式）

| 项 | 规格 |
|---|---|
| 传输 | Windows 命名管道；macOS/Linux Unix socket |
| 协议 | 逐行 JSON；命令带 `request_id`，事件无 ID |
| 鉴权 | 仅本用户可访问（socket 权限 0600；管道 ACL 当前用户）；不监听 TCP |
| 命令白名单 | loadfile/seek/pause/set_property/get_property/screenshot；禁止 `run`/`subprocess`/`script` |
| 超时 | 命令 2s；事件泵 1s 心跳 |
| 用途 | CI、自动化测试、外部控制适配；**不用于正常播放** |

### 5.5 配置与状态存储

| 类型 | 载体 | 内容 | 规则 |
|---|---|---|---|
| 用户设置 | `settings.toml`（带 `schema_version`） | 外观、快捷键、音频设备、HDR 策略、网络、日志级别 | 原子写（临时文件 + rename）；启动时迁移；未知键保留 |
| 主题选择与布局状态 | `settings.toml` 的 `[ui]` / `[ui.layout.<theme>]` | `theme`（A/B/C）、检查器宽度与可见性、紧凑断点覆盖值 | 缺键默认 A；非法/缺失主题回退 A；按主题分区保存，互不覆盖 |
| 播放状态 | `state.db`（SQLite，WAL） | 历史、续播位置（每 2s 去抖写）、书签、播放列表、最近文件 | 键规则见下；崩溃后可恢复；定期 VACUUM |
| 窗口状态 | `settings.toml` 的 `[ui.window]` | 位置、大小、最大化/全屏、置顶、全屏隐藏鼠标（延迟/阈值） | 显示器拓扑变化后回落到可用屏幕内（§11.10） |
| 缓存 | `cache/` | 缩略图雪碧图、**海报档位图**、字幕缓存、shader 缓存 | 可清理，设上限 LRU |
| 机密 | 系统钥匙串 | SMB/WebDAV 凭据、媒体服务器 token/JWT、PIN 设备标识 | 绝不落盘明文，也不进日志 |
| 媒体服务器配置 | `settings.toml` 的 `[media.servers.<id>]` | 类型、地址、端口、pathPrefix、userId、显示名、能力缓存、自动发现开关 | 此处**只放非机密**；缺键即未配对 |
| 媒体库缓存 | `media.db`（SQLite）+ `cache/media/` | 库条目、进度合并记录、海报引用 | 元数据带 TTL/ETag；海报按档位 LRU（§8.5） |
| mpv 覆盖 | `mpv-extra.conf`（受限） | 高级用户选项 | 白名单/黑名单校验后注入 |

不直接把 mpv 的 `mpv.conf` 当应用配置：mpv 选项会随版本漂移，应用配置必须是我们自己的稳定 schema。

**进度记忆键（§11.8 硬约束）**：

| 来源 | 键 | 说明 |
|---|---|---|
| 本地文件 | `sha256(规范化绝对路径)` 取前 128 位 | Windows 统一小写盘符、正斜杠、去尾随分隔符 |
| 移动/重命名 | 同大小 + 同 mtime + 同前 64KB 哈希弱匹配 | 命中才迁移，避免误合并 |
| 网络直链 | `sha256(规范化 URL，剔除 token/sig/expires)` | 临时签名参数不得进入键 |
| 媒体库条目 | `(serverId, itemId)` | 不用播放 URL（直连与转码 URL 不同） |

**禁止**用播放列表序号、数组下标、显示标题或无扩展名文件名作键；无法规范化时不写记忆并记诊断日志。

版本与迁移：`settings.toml` 使用 `schema_version`，SQLite 使用 `PRAGMA user_version`，两者独立演进、互不替代。两类变更要区别对待：

- **主题（§7.5）是内容级扩展，不提升版本**：加载器遇到缺键直接取默认 `"A"`，非法值同样回退 `"A"` 并记一条诊断日志；布局持久参数按主题分区，新增主题不需要迁移旧配置。只有主题 ID 语义变更（例如 `A` 改名为 `classic`）才需要一次映射迁移（`schema_version` +1）；主题 ID 从 1.0 起冻结。
- **媒体库（§8）是结构性变更，必须提升版本**：新增 `media.db` 的服务器表、条目缓存表、进度合并表与索引，`PRAGMA user_version` +1；`settings.toml` 新增 `[media.*]` 分区，`schema_version` 同步 +1。迁移只建表/建索引，不回填历史数据；回滚（down）删表即可，不影响 `state.db` 的播放历史与续播。缺少 `media.db` 时以空库启动，不阻塞播放。

原则：**播放路径不依赖媒体库**。媒体库迁移失败、数据库损坏或未登录时，应用必须仍能以“本地文件播放器”形态完整启动。

### 5.6 快捷键系统

- 动作模型：`ActionId + scope + 默认键 + 平台修饰键映射`；作用域：全局、播放器、全屏、文本输入、对话框。
- 解析顺序：文本输入 > 对话框 > 全屏 > 播放器 > 全局；同作用域内“完全匹配优先”，冲突在保存时阻断并提示。
- 用户覆盖存 `settings.toml` 的 `[shortcuts]`，支持导入/导出；显示时转成平台符号（⌘⌥⌃ / Ctrl/Alt/Win）。
- 媒体键：Windows SMTC、macOS MPRemoteCommandCenter、Linux MPRIS（D-Bus）；不走 libmpv 的键盘处理。
- 全局热键：Windows `RegisterHotKey`；macOS `NSEvent` 全局监听（需要辅助功能权限时明确提示）；Linux X11 grab / Wayland `GlobalShortcuts` portal（每次授权）。
- mpv 自身的 `input.conf` 只用于内置脚本，不与应用快捷键争抢。

### 5.7 插件与脚本扩展点

| 层级 | 形态 | 能力 | 隔离 |
|---|---|---|---|
| L1 | mpv Lua/JS 脚本（`--script`） | 自动加载、字幕/章节处理、着色器切换等播放行为 | 同进程，脚本异常可能拖垮 core；仅允许用户显式安装，默认关闭 |
| L2 | 应用插件（P2，WASM） | 菜单项、设置页、字幕源、URL resolver、控制层按钮 | WASM 沙盒 + 能力声明（文件/网络/命令白名单），独立线程池与超时 |
| L3 | 伴随进程（yt-dlp/ffmpeg） | 站点解析、转码录制、缩略图批量生成 | 严格参数、无 shell、超时与退出码检查 |

原则：不向插件暴露 `mpv_handle` 裸指针；所有能力经 `PlayerFacade` 的白名单方法。L2 是 1.x 的扩展方向，1.0 只做 L1 + L3。

### 5.8 崩溃恢复

1. **面包屑**：看门狗每 2s 写 `{file, pos, playlist, backend, gpu, driver, hwdec, settings_hash}` 到 WAL。
2. **异常捕获**：Crashpad/Breakpad 生成 minidump，**只存本地、默认不上传**；保留最近 5 份，30 天自动清理；诊断页可见路径并提供**手动导出诊断包**入口（默认脱敏，§11.11）。
3. **安全模式**：10 分钟内两次异常启动 → 以 `--no-config`、`hwdec=no`、禁用 shader、GL 后端、甚至 SW 呈现启动；UI 明示“安全模式”。
4. **设备丢失**：D3D11 捕获 `DXGI_ERROR_DEVICE_REMOVED`，GL 捕获上下文丢失；销毁并重建 Presenter，重新 loadfile 到原位置（目标 10s 内恢复）。
5. **解码看门狗**：N 秒无帧前进或 hwdec 初始化连续失败 → 自动切 `*-copy` 或软解，提示一次。
6. **会话恢复**：重启后恢复播放列表与位置；不自动播放，等用户确认（避免惊吓）。

注意：libmpv 官方声明“内存耗尽时会杀掉进程”，且会改动信号处理/`timeBeginPeriod` 等全局状态——这就是为什么**不能**把崩溃隔离寄托在“同进程多 core”，也不要幻想 IPC 能解决。

### 5.9 规划中的目录结构（仅示意，不建仓）

```
src/
  app/            # 启动、窗口、QML 注册、路由
  ui/             # QML：tokens / components / layouts / shell / 设置页 / 媒体库视图
  core/           # 播放域：状态机、队列、历史、书签
  mpv/            # MpvClient、事件泵、属性映射、兼容表
  render/         # IVideoPresenter + 平台实现
  media/          # 内容源层（§8）：MediaLibraryService、规范模型、适配器、进度合并、海报缓存
  platform/       # win / mac / linux 原生薄层
  persist/        # settings、SQLite、迁移
  plugins/        # L1 脚本管理、L3 进程管理
  diagnostics/    # 日志、crash、指标
tests/
  unit/           # core/persist/快捷键/进度合并
  contract/       # 适配器契约测试（同一组用例跑三套适配器，含录制回放夹具）
  integration/    # 媒体矩阵、headless mpv、媒体服务器联调
  ui/             # QML 冒烟与截图基线
```

---

### 5.10 产品命名与标识（Palune）

产品名为 **Palune**（英文正写、首字母大写）。用户可见文本、安装包、日志、崩溃目录、更新源统一使用该名；仅在标识符、Linux 命令与路径中使用小写 `palune`。

**应用标识（发布后不可回退）**

| 平台 | 标识 | 用途 | 可改性 |
|---|---|---|---|
| macOS | `app.palune.player` | `CFBundleIdentifier`：钥匙串 ACL、TCC 授权、公证票据、更新签名、崩溃符号 | **发布后不可改** |
| Windows | `app.palune.player`（AppUserModelID）；若上 Microsoft Store 另用 `Palune.Player`（Package Identity） | 任务栏分组、通知、文件关联、协议注册 | 商店身份发布后不可改；AppUserModelID 改动会重置通知与关联 |
| Linux | `app.palune.player`（Flatpak app-id、`app.palune.player.desktop`） | Flatpak 发布身份、MIME、DBus | Flathub 发布后不可改 |

选择理由：三平台统一反向 DNS，唯一且可注册，与域名同源；不采用 `com.palune.player`，避免与 iOS/Android 生态的 bundle id 习惯混淆；也不用品牌无关的 `player` 前缀（会与系统组件重名）。

自定义协议统一为 `palune://`：Windows 注册到 `HKEY_CLASSES_ROOT\palune`，macOS 写 `CFBundleURLSchemes`，Linux 用 `x-scheme-handler/palune`。协议名与标识一并冻结。

**硬约束**：`app.palune.player` 在**任何外部分发（公证、真实账号登录、公测）之前冻结**。改名会导致钥匙串凭据全部失效、TCC 需重新授权、公证票据与更新链重建，因此不得把“先随便用一个，之后改”写进任何排期。

**可执行文件名与安装包**

| 项 | 规则 |
|---|---|
| Windows | `Palune.exe`；默认安装目录 `%ProgramFiles%\Palune`，每用户安装为 `%LOCALAPPDATA%\Programs\Palune` |
| macOS | `Palune.app`（可执行文件 `Contents/MacOS/Palune`）；`CFBundleName` / `CFBundleDisplayName` 均为 `Palune` |
| Linux | `palune`（小写，遵循发行版惯例）；桌面项 `Name=Palune` |
| 安装包 | `Palune-<version>-win-x64.exe`、`Palune-<version>-win-arm64.exe`、`Palune-<version>-mac-universal.dmg`（或分架构 `-mac-arm64.dmg` / `-mac-x64.dmg`）、`Palune-<version>-linux-x86_64.AppImage`；便携版 `Palune-<version>-win-x64-portable.zip` |
| 版本 | SemVer `MAJOR.MINOR.PATCH`，预发布 `-beta.N`；文件名、清单与关于页版本号必须逐字一致 |

**配置与数据目录**

| 平台 | 配置（settings.toml / state.db） | 缓存与媒体库 | 日志与崩溃 |
|---|---|---|---|
| Windows | `%APPDATA%\Palune\` | `%LOCALAPPDATA%\Palune\cache\`、`%LOCALAPPDATA%\Palune\media.db` | `%LOCALAPPDATA%\Palune\logs\palune.log`、`%LOCALAPPDATA%\Palune\crashes\` |
| macOS | `~/Library/Application Support/Palune/` | `~/Library/Caches/app.palune.player/` | `~/Library/Logs/Palune/palune.log`、系统 `~/Library/Logs/DiagnosticReports/` |
| Linux | `$XDG_CONFIG_HOME/palune/`（默认 `~/.config/palune/`） | `$XDG_CACHE_HOME/palune/`（默认 `~/.cache/palune/`） | `$XDG_STATE_HOME/palune/logs/`（默认 `~/.local/state/palune/logs/`） |

路径大小写规则：Windows/macOS 目录用 `Palune`，Linux 与所有标识符用 `palune`；日志前缀固定 `palune`。

**首次启动的代号迁移**

- 仅针对“曾用代号跑过测试版”的环境；全新安装不触发迁移。
- 迁移内容：`settings.toml`、`state.db`（历史/书签/续播）、`[ui.window]`、快捷键绑定、`media.db` 与 `cache/media/`。
- 流程：读旧目录 → 复制到新目录（**不删除源**）→ 写 `migration.done` 标记 → 校验条目数与键可读 → 失败则回滚为“空配置 + 一次性提示”。
- 凭据：钥匙串服务名由旧名改为 `palune`。若因标识变更导致 ACL 失效，**要求用户重新登录**，不静默复制密码、不导出 token。
- 迁移不得覆盖已存在的新目录；完成后仅一条非模态提示（“已从旧版本迁移设置”），不阻塞播放。

**更新通道**

设计冻结的标识为 `updates.palune.app`（stable/beta 两个 feed），但**当前实现并未使用该域名**：域名未注册/未受控，1.0.0-RC 起实际更新源是源码仓自身的 GitHub Release。以下"实际实现"为准，避免文档里出现两套说法。

| 项 | 设计冻结值 | 实际实现（2026-11-08 起） |
|---|---|---|
| stable | `https://updates.palune.app/stable/latest.json` | `https://github.com/yunyu697/palune/releases/latest/download/latest.json` |
| beta | `https://updates.palune.app/beta/latest.json` | `https://github.com/yunyu697/palune/releases/latest/download/latest-beta.json` |
| 产物 | `https://updates.palune.app/<channel>/<version>/<安装包文件名>`，版本目录只增不改 | `https://github.com/yunyu697/palune/releases/download/<tag>/<安装包文件名>`；Release 只增不改 |
| 清单字段 | `version`、`minOs`、`arch`、`url`、`sha256`、`signature`、`notesUrl`、`publishedAt` | 信封 `payload` + `signature`；payload 含 `schemaVersion`、`channel`、`version`、`publishedAt`、`notes`、`artifacts[]`（`platform`/`arch`/`url`/`size`/`sha256`/`installer`/`minOsVersion`/`signature`），非主格式在 `extraArtifacts` |
| 预发布 | — | alpha / beta / RC 一律 GitHub `prerelease`，不接管 `releases/latest`；只用 tag 定点清单 |
| 签名校验 | Ed25519 签名清单；公钥编译期内置于 `resources/trust/update.pub`，**禁止运行时下载公钥** | 同左（已实现）；清单与每个产物各一份签名，客户端逐源验签 |
| 托管 | 对象存储 + CDN，`latest.json` 与产物同源；保留上一版本用于回滚；可选国内镜像作为备用源 | GitHub Release（匿名）；备用源为内置 `ghfast.top` 前缀镜像与 `PALUNE_UPDATE_MANIFEST_MIRRORS`；旧仓 `palune-releases` 已归档，不再是源 |

后续若启用 `updates.palune.app`，属于**更新源迁移**：需同时改客户端默认地址、重新签发清单，并评估已安装客户端的可达性；不得只改 DNS 而让旧地址断链。

**命名约束（备案）**

- **商标检索尚未完成**：第 9 类（软件）官方检索被验证码阻断。1.0 公开发布前必须完成专业检索（至少 CN / US / EU），未通过则不得商用发布与开设商店页；内部与测试版本可继续使用 Palune。
- `palune.app`（更新源与官网）与 `palune.dev`（文档/开发者）需**尽早注册**，`updates` 作为子域；备用域 `getpalune.com`、`paluneplayer.com` 一并占位。
- 同步占位：GitHub org、Homebrew cask（`palune`）、winget（`Palune.Player`）、Flatpak app-id、Snap（`palune`）、商店与社交账号。
- 商标未清前，关于页与安装包说明保留“名称可能变更”；1.0 发布后 `app.palune.player`、包名与域名不得再改。

---

## 6. 界面与布局主题（A/B/C 三套并存）

### 6.1 视觉令牌（主题无关的共享层）

以下为三套主题共享的默认值；主题只能通过令牌覆盖颜色、字体、圆角、密度与动效，**不能用令牌去表达结构差异**（结构差异属于 §7.2 的布局模式层）。

| 项 | 规格 |
|---|---|
| 基准窗口 | 1280×720，最小 480×270，16:9 优先，自由比例 |
| 圆角 | macOS 10 / Windows 8 / Linux 12（无边框 CSD） |
| 标题区高度 | A/B 为 44px；C 为 36px（见 §6.2） |
| 控制区高度 | A 为 64px 浮层；B 为 72px dock；C 为悬浮胶囊（见 §6.2） |
| 主色 | 近黑 `#0E0E12`；面板 `#16161C`；强调色取系统强调色（可由设置覆盖） |
| 字体 | macOS `SF Pro`，Win `Segoe UI Variable`，Linux `Inter/Noto Sans`；标题 13/600，时间码 12/500 等宽数字 |
| 间距 | 8pt 网格（8/16/24/32）；紧凑密度下 4pt 网格 |
| 动效 | 180ms，`cubic-bezier(.2,.8,.2,1)`；控制层淡入淡出；抽屉 220ms |
| 焦点 | 2px 强调色描边 + 1px 内阴影，键盘可见 |

### 6.2 A / B / C 三套布局

**A（默认，浮层式）**：控制层覆盖在视频上，滑出播放列表不改变视频布局。

```
┌────────────────────────────────────────────────────────────┐
│ ● ● ●   Interstellar (2014).mkv             ⤢   ⛶   ⋯     │ 44px 浮层
├────────────────────────────────────────────────────────────┤
│                                                            │
│                                                            │
│                  视频 / 字幕（可用面积 100%）                │
│                                                            │
│     ┌──────────────────────────────┐                       │
│     │ 2.0x · 音轨 2/3 · 字幕 中文   │  轻提示（1.8s 消失）   │
│     └──────────────────────────────┘                       │
│                                                            │
├────────────────────────────────────────────────────────────┤
│  ▶   00:12:34  ━━━━━━━●──────────────  01:48:20   🔊  ⛶   │ 64px OSC 浮层
└────────────────────────────────────────────────────────────┘
```

**B（停靠式）**：右侧检查器常驻，底部 dock 不覆盖视频；播放/跳转/音量/速度合并为 transport。

```
┌──────────────────────────────────────────┬─────────────────┐
│ ● ● ●  Interstellar (2014).mkv     ⛶ ⋯  │ 检查器（常驻）   │
├──────────────────────────────────────────┤ 340px，可折叠   │
│                                          │                 │
│                                          │ 播放信息        │
│        视频（可用面积 = 窗口 − 顶 − 底 − 右）│ 音轨 / 字幕     │
│                                          │ 章节            │
│                                          │ 快捷键提示      │
├──────────────────────────────────────────┴─────────────────┤
│ ▶ ⏮ ⏭ │ 00:12:34 ━━━●━━━━━ 01:48:20 │ 🔊 2.0x │ 列表 │     │ 72px dock
└────────────────────────────────────────────────────────────┘
```

**C（沉浸式）**：顶部细进度条 + 悬浮胶囊，控制层几乎不占画面；播放列表为全屏 overlay。

```
┌────────────────────────────────────────────────────────────┐
│ ● ● ●  Interstellar    ━━━━━━●──────────────         ⋯     │ 36px 顶栏 + 细进度
├────────────────────────────────────────────────────────────┤
│                                                            │
│                                                            │
│                   视频（可用面积最大）                       │
│                                                            │
│                                                            │
│              ┌──────────────────────────┐                  │
│              │  ▶   00:12:34    🔊     │  悬浮胶囊（自动隐藏）│
│              └──────────────────────────┘                  │
└────────────────────────────────────────────────────────────┘
```

| 维度 | A（默认） | B | C |
|---|---|---|---|
| 视频可用面积 | 100%（控制层浮在画面上） | 窗口 − 标题 − dock − 检查器 | 约 100%（仅 36px 顶栏覆盖） |
| 控制层 | 底部 OSC 64px，自动隐藏 | 底部 dock 72px，常驻 | 悬浮胶囊，自动隐藏 |
| 控件分组 | 不合并，单行排列 | 播放/跳转/音量/速度合并为 transport | 仅保留播放/时间/音量，其余进“更多” |
| 进度条形态 | 底部横向条 | dock 内横向条 | 顶部细线，悬停展开为大热区 |
| 检查器 | 右侧抽屉 320px，覆盖式 | 常驻 340px，可折叠 | 全屏 overlay，按需打开 |
| 播放列表 | 右侧抽屉，覆盖式 | 检查器内 Tab（与章节并列） | 全屏 overlay（居中卡片） |
| 紧凑形态（见 §7.4） | OSC 精简为两行内 | 检查器折叠为底部 sheet | 胶囊缩为单个播放按钮 |
| 适合场景 | 默认、单文件观看 | 调字幕/音轨、信息密集 | 沉浸、小窗、投影 |

三套主题共享：设置页、错误页、空状态、轻提示、上下文菜单、快捷键系统、所有业务动作。它们属于组件层，不随布局变化重写。

### 6.3 交互状态机（可判定的行为）

| 状态 | 触发 | UI 行为 |
|---|---|---|
| 未加载 | 启动 | 空状态，不显示控制层 |
| 加载中 | 打开文件 | 半透明骨架（最多 800ms），可取消 |
| 播放中 | 首帧 | 控制层（OSC / dock / 胶囊）按各自规则自动隐藏，光标 2s 后隐藏 |
| 暂停 | 空格/点击 | 控制层常驻，标题显示暂停图标，**不改变画面颜色** |
| Seek | 拖动进度条 | 显示目标时间 + 缩略图；松手才提交精确 seek |
| 缓冲 | `paused-for-cache` | 小型 spinner + “缓冲中”，不弹模态 |
| 出错 | 加载失败 | 错误页；自动回退按钮可用 |
| 全屏 | 双击/⛶ | 隐藏标题区；控制层按各自热区唤出 |
| 迷你 | ⤢ 长按/菜单 | 320×180 无边框置顶，仅保留悬停控制 |

### 6.4 平台差异

- macOS：交通灯、`NSVisualEffectView` 毛玻璃、窗口拖动区、全屏空间；EDR 模式下 UI 仍为 SDR 合成（系统负责）。
- Windows：自绘最小化/最大化/关闭；覆盖窗与视频窗同步；Snap Layouts 支持；Mica 可选但不得影响视频合成。
- Linux：客户端装饰，圆角与阴影自绘；Wayland 下不做“始终置顶”的强保证；模糊效果可选（无合成器支持时退化为纯色半透明）。
- 窗口状态（三平台一致，§11.10）：恢复时校验几何是否落在当前可用屏幕内，显示器拓扑变化（拔插、DPI/分辨率变化、主屏切换）后回落到主屏居中并保持宽高比；全屏隐藏鼠标的延迟与阈值可配，退出全屏恢复原几何。

### 6.5 UI 性能纪律

- 视频帧只由 Presenter 更新，QML 不参与每帧重绘；控制层时间码 4Hz 更新，与主题无关。
- 视频上方禁止全屏 `MultiEffect`/模糊；B 的常驻检查器与抽屉的模糊只作用于静态区域并缓存。
- 所有覆盖层默认 `visible: false`，按需实例化；列表用 `ListView` 复用委托。
- 动画只改 `opacity/transform`，不改布局属性；切换主题的淡入淡出不触发视频重绘。

---

## 7. 主题机制（A/B/C 布局级主题）

### 7.1 性质：结构差异，不是令牌替换

A/B/C 在以下**结构维度**上不同，这些差异无法用 QSS/CSS 变量表达：

| 结构维度 | A | B | C |
|---|---|---|---|
| 区域树 | 视频 + 浮层控制 + 覆盖抽屉 | 视频 + 常驻右栏 + 常驻底栏 | 视频 + 顶栏 + 浮层胶囊 + 全屏 overlay |
| 视频布局约束 | 占满窗口，控件覆盖 | 按剩余矩形收缩 | 占满窗口，顶栏覆盖 |
| 控件组合方式 | 单行原子控件 | 合并为 transport 复合组件 | 只留核心控件，其余进菜单 |
| 进度条组件 | 底部横向条 | dock 内横向条 | 顶部细线 + 悬停展开 |
| 焦点顺序 | 底栏 → 抽屉 | 视频 → 右栏 → 底栏 | 顶栏 → 胶囊 → overlay |
| 紧凑形态 | 底栏精简 | 检查器折叠为 sheet | 胶囊缩为单按钮 |

**因此不采用“三套 QSS/CSS 覆盖”**：

1. 样式表只能改变已有控件的外观，不能改变**控件树**、控件分组与视频可用面积；B 的常驻检查器与 C 的顶部进度条需要不同的组件树。
2. 覆盖式主题是“对基础样式的 diff”，选择器特异性会随主题数量失控；基础组件任何改动都要在三套覆盖里重新验证。
3. 焦点顺序、无障碍路径、截图基线都是结构相关的，样式层无法统一表达。
4. 组件会被迫知道“我属于底部控制条”，一旦换布局就要复制组件，最终退化成三套独立 UI。

结论：**令牌只用于同一布局下的配色/密度变体；A/B/C 走结构分层。**

### 7.2 四层划分与禁止跨层依赖

| 层 | 职责 | 允许依赖 | 禁止 |
|---|---|---|---|
| L0 设计令牌层 | 颜色、字体、间距、圆角、阴影、密度、动效曲线；只读数据 | 无 | 引用任何组件/布局；包含位置或尺寸语义（如“底栏高度”）；包含主题 ID |
| L1 组件层 | 原子与复合组件：按钮、滑块、时间码、音量、轨道菜单、`TransportCluster`、`InspectorPanel`、`PlaylistView`、`LibraryGrid`、`ItemDetail`、`Toast` | L0 | 知道“底部/右侧/顶部”等位置；读取窗口尺寸或主题 ID；直接调用 `MpvClient`；把业务命令写进组件 |
| L2 布局模式层 | `LayoutA` / `LayoutB` / `LayoutC`：区域划分、几何、可见性、z-order、紧凑变体；提供 `VideoSlot` 等区域契约 | L0 + L1 | 包含业务逻辑（只发 ActionId）；直接操作播放器或文件；感知主题名称常量 |
| L3 主题装配层 | `ThemeRegistry` + `LayoutHost`：主题 ID → 令牌集 + 布局组件 + 平台覆盖 + 降级策略；切换流程的唯一入口 | L0/L1/L2 + 平台能力查询 | 包含具体布局细节；包含业务逻辑；被其他层反向引用 |

依赖方向严格单向：**L3 → L2 → L1 → L0**。反向 import、L1 读取 `themeId`、L2 直接 seek、L0 出现布局常量，都在代码评审中直接拒绝。

补充两个主题无关但必需的支撑：

- **`VideoHost`**：拥有原生视频 surface 的对象，位于布局树之外；L2 只提供 `VideoSlot` 矩形，L3 负责把 `VideoHost` 绑定到当前槽位。
- **`UiStateStore`**：保存语义化 UI 状态（列表滚动索引、当前选中项、焦点 ActionId、检查器宽度）；L2 读写它，L1 不直接持久化任何东西。

组件层的硬性契约：

- 必须暴露 `implicitWidth/implicitHeight`、`minimumWidth/minimumHeight`，由布局决定最终几何。
- 复合组件只能在自己的**内部**做局部排列（例如 transport 内部的按钮顺序），不得引用窗口边缘或外部区域。
- 所有动作通过 `ActionId` 发出，不直接调用播放器；这样同一组件在 A/B/C 三处行为一致。
- 不读取 `themeId`；需要差异化时由 L2 传入显式参数或令牌。

### 7.3 QML 落地：共享树 + 只切布局模式

| 方案 | 做法 | 优点 | 代价 | 结论 |
|---|---|---|---|---|
| 1. Loader 切整棵 UI | 每套主题一个根组件，切换即替换整树 | 各主题互不干扰，起步快 | 播放列表滚动、焦点、抽屉/检查器状态全部丢失；组件重复实现；切换闪烁；三套视觉逐渐分叉 | 不采用 |
| 2. 同一组件树 + 布局策略对象 | 单一布局组件读取 strategy（区域位置/可见性） | 分支集中 | 无法表达 B 的常驻区、C 的顶部进度条等结构差异，最终变成大量 `if theme` | 不作为主方案 |
| 3. 共享外壳 + Loader 只切 `LayoutMode`（**推荐**） | `VideoHost`、`PlayerFacade`、`UiStateStore`、`ToastHost` 在布局树外；`Loader` 仅加载 `LayoutA/B/C`；布局组合共享组件 | 状态保留、结构自由、组件复用高、新增主题不动播放层 | 需要区域契约与状态仓；主题作者必须遵守组件契约 | 采用 |

推荐结构（概念层，不写代码）：

```
WindowShell（常驻）
├── VideoHost（原生 surface，永不重建）
├── UiStateStore / Actions / ToastHost（常驻）
├── LayoutHost
│   └── Loader → LayoutA | LayoutB | LayoutC
│        ├── VideoSlot（只提供矩形）
│        ├── 控制区（组合共享组件）
│        └── 检查器 / 抽屉 / overlay（组合共享组件）
└── 平台 chrome（交通灯 / 覆盖窗 / CSD）
```

**切换流程（顺序固定，且全程不触碰播放层）**：

1. 关闭当前打开的菜单/拖动状态；控制层淡出 120ms。
2. 记录 `UiStateStore` 快照（滚动索引、选中项、焦点 ActionId、各面板宽度）。
3. `Loader` 卸载旧 `LayoutMode`（只销毁 UI 对象），`VideoHost` 保持存活。
4. 加载新 `LayoutMode`；`VideoSlot` 布局完成后，L3 把 `VideoHost` 更新到新矩形（Windows 是 `SetWindowPos`，mac/Linux 是几何/z-order 更新）。
5. 恢复滚动位置（`positionViewAtIndex`）、焦点（按 ActionId 映射）与面板宽度；控制层淡入。
6. 写入 `settings.toml` 的 `[ui] theme`；失败则回退 A 并提示一次。

**播放不中断的证据（架构不变量 + 可执行断言）**：

- `PlayerFacade` / `MpvClient` / 渲染上下文生命周期属于应用，不属于 `LayoutHost`；切换路径禁止调用 `loadfile`/`stop`/`quit`，禁止重建 render context。
- 视频 surface 不随布局销毁；只更新几何（必要时更新 z-order），**不改变父级**。mac/Linux 的 FBO item 始终挂在 `WindowShell` 根节点，Windows 的 `wid` 视频窗只做 `SetWindowPos`。
- 自动化断言：4K60 播放中执行 A→B→C→A 共 20 次，要求 `time-pos` 单调、无 `end-file`/`loadfile` 事件、音频无 > 20ms 断点、每次切换黑帧 ≤ 100ms、`frame-drop-count` 增量 ≤ 2/次、20 次后 RSS 增量 ≤ 10MB。

**状态保持清单**：

| 状态 | 存放位置 | 切换主题时 |
|---|---|---|
| 播放位置/暂停/速度/音轨/字幕轨 | mpv core + `PlayerFacade` | 不受影响（UI 只重新绑定显示） |
| 播放列表内容与当前项 | core 模型 | 不变 |
| 列表滚动位置、选中项 | `UiStateStore`（会话态） | 恢复；越界时夹取到最近项 |
| 检查器宽度/可见性/Tab | `settings.toml` 按主题分区 | 每个主题使用自己的值，互不覆盖 |
| 键盘焦点 | `UiStateStore` 的 ActionId 映射 | 恢复；新布局无此控件时聚焦最近的等价控件（如播放按钮） |
| 打开的菜单/拖拽 | 组件临时态 | 切换前关闭/取消（不恢复） |
| 轻提示/OSD | `ToastHost`（布局树外） | 保留，不重建 |
| 窗口几何/全屏/迷你 | 窗口控制器 | 不变 |

代价（必须承认）：区域契约、状态仓、焦点映射都是额外设计；主题作者不能“想放哪就放哪”，必须把控件放到布局层提供的槽位里。换来的收益是：新增第 4 套主题只需写一个 `LayoutMode` + 令牌覆盖，不复制组件、不动播放代码。

### 7.4 自动降级（紧凑模式）

- **归属层**：判定与形态在 **L2 布局模式层**；L3 只提供窗口逻辑尺寸、全屏/迷你状态与断点配置；L1 只接受 `density=compact/normal` 令牌，不做判定。
- **两级降级**：每个布局有独立的紧凑变体（A 精简底栏、B 检查器折叠为底部 sheet、C 胶囊缩为单按钮）；同时公共组件按 `density` 缩小间距与字号，避免只改结构不改密度。
- **防抖**：窗口尺寸变化做 150ms 尾部防抖；连续两帧尺寸稳定后才评估一次，避免拖动过程中反复重排。
- **滞回**：进入与退出使用不同阈值，默认宽 `< 720` 进入、`> 800` 退出（80px 回差）；高 `< 560` 进入、`> 620` 退出。阈值比较使用**逻辑像素**，不受 DPI 影响。
- **状态机**：`normal ↔ compact` 每个轴独立；只在跨越“进入阈值 + 退出阈值”时改变，阈值区间内保持原状态；切换主题时重新评估一次但沿用当前窗口尺寸。
- **全屏/迷你**：全屏沿用同一套阈值（大屏通常不触发）；迷你窗强制 compact；切换过程中（全屏动画/窗口拖动）挂起评估。
- **可配置**：`[ui.compact] enterWidth=720, exitWidth=800, enterHeight=560, exitHeight=620, debounceMs=150`；校验规则 `exit ≥ enter + 40`、范围 320–2560，非法回退默认值并在设置页提示。
- **验收**：在阈值附近以 1px 步进、10Hz 来回拖动 50 次，稳定后模式切换 ≤ 1 次；重排后截图基线稳定，无闪烁；紧凑切换的 UI 帧时间 < 16.7ms。

### 7.5 配置与持久化

| 项 | 键 | 默认 | 规则 |
|---|---|---|---|
| 主题选择 | `[ui] theme` | `"A"` | 枚举 A/B/C；缺失或非法 → A；用户切换后立即写盘（1s 去抖） |
| 布局持久参数 | `[ui.layout.B] inspectorWidth/inspectorVisible/inspectorTab` 等 | 各主题内置默认 | 按主题分区，互不覆盖；新增主题不需要迁移旧键 |
| 紧凑断点 | `[ui.compact] enterWidth/exitWidth/enterHeight/exitHeight/debounceMs` | 见 §7.4 | 校验失败回退默认；写入前归一化 |
| 恢复默认 | 设置页“恢复默认主题” | — | 删除 `[ui.layout.*]` 覆盖并写回 `theme="A"`，不要求重启 |

迁移策略：沿用既有版本机制但不强行升级——`settings.toml` 的 `schema_version` 与 SQLite 的 `PRAGMA user_version` 独立；新增 `ui.theme` 是内容级扩展，**两个版本号都不递增**（没有表结构变化）。只有主题 ID 语义改名时才写一条迁移（旧值 → 新值，`schema_version` +1）；主题 ID 自 1.0 冻结。配置损坏/主题资源缺失时，启动流程必须仍能进入 A，不允许因主题问题阻塞播放。

### 7.6 成本增量

以“只做 A”的 UI 层工作量为 100% 作为基准：

| 模块 | 一次性（三主题共用） | 每套主题边际 | 说明 |
|---|---|---|---|
| 令牌层与主题注册表 | 5–7% | — | A 也需要令牌；增量在于主题覆盖与注册表 |
| 组件库“布局无关”改造 | 18–22% | — | 最大的一次性成本：去掉“底栏/右侧”假设，补槽位与契约 |
| 区域契约 + `LayoutHost` + 切换流程 | 12–15% | — | 含 `VideoHost` 重绑、切换时序、失败回退 |
| `UiStateStore` + 焦点/滚动恢复 | 8–10% | — | 保证切换不丢状态，必须单独设计与测试 |
| 紧凑模式与滞回 | 4–5% | — | 断点状态机、防抖、配置校验 |
| 配置/迁移/损坏回退 | 2–3% | — | 键设计、默认值、回退路径 |
| 布局 A | 0（基线） | — | 已计入 100% |
| 布局 B / 布局 C | — | 各 12–16% | 结构重写 + 视觉适配，不只是改颜色 |
| 每主题视觉设计与评审 | — | 4–6% | 三套都要过设计评审 |
| 每主题截图基线与回归 | 8%（框架） | 3–4% | 框架一次性，基线随主题增长 |

汇总：

- **UI 层**相对“只做 A”增加 **85–105%**（接近翻倍）；折算到全项目约 **+20–26%**（UI 约占总工程量的四分之一）。
- 其中约 **55%** 是**一次性**成本（组件契约、装配、状态恢复、降级、配置）；约 **45%** 是 B/C 的边际成本。
- 新增第 4 套主题的边际成本约 **12–16%**（UI 基线），不含设计评审与截图回归时更低。
- 若跳过分层、直接写三套覆盖：短期看似省 10–15%，但基础组件每次改动要在三套里分别回归，M3 之后开始倒亏；这正是不采用 QSS/CSS 覆盖的经济原因。

---

## 8. 媒体库接入：海报墙与播放进度同步

> 依据《媒体服务器接入可行性调研》（`MEDIA_SERVER_FEASIBILITY.md`）的实测与官方文档结论：五家服务端最终映射为**三套适配器**——Jellyfin / Emby 家族（覆盖 Jellyfin、Emby、极空间、飞牛）、Plex 独立适配器。调研中标注“未证实/待实测”的项目，在本方案里一律按“能力探测 + 降级”处理，不写成假设。

### 8.1 分层位置与边界

媒体库是**独立于播放内核的内容源层**，不是播放域的一部分，也不是 UI 的一部分：

| 层 | 负责 | 不负责 |
|---|---|---|
| UI（QML 组件） | 海报墙、详情页、筛选/排序、服务器配对向导、进度提示 | 不拼 URL、不解析服务器响应、不持 token、不做进度合并 |
| `MediaLibraryService`（应用服务层） | 会话与凭据、能力矩阵、库视图聚合、**进度回传调度与合并**、离线队列、缓存策略 | 不打开播放器、不决定解码方式 |
| `core` 播放域 | 播放状态机、队列、续播位置（本地）、书签 | 不知道媒体服务器存在，不 import 适配器 |
| `MpvClient` / `Presenter` | 解码与呈现；向服务层报告 `time-pos`/`duration`/暂停/seek | 不感知库语义，不构造服务器请求 |
| 适配器（内容源层） | 协议实现、字段映射、能力探测、直链拼装、上报端点 | 不做 UI、不缓存图片（交给缓存服务）、不持有全局状态 |

**“播放位置”归属（关键边界）**：

1. **播放期间**，位置的事实来源只有一个——mpv core（`time-pos`、`duration`、`pause`）。服务层订阅这些属性，不做二次推算。
2. **回传**由 `MediaLibraryService` 负责：按“10 秒节流 + 暂停/恢复/seek/切轨立即上报”的规则，把毫秒位置映射成该协议需要的单位（Jellyfin/Emby `PositionTicks` 100ns、Plex 毫秒），并管理 `PlaySessionId`、失败重试与合并（§8.6）。
3. **回读**发生在“打开条目之前”，用于决定续播起点；一旦进入播放，服务端的后续变化不再反向改写本地位置。
4. 本地 `state.db` 仍是**离线与冲突兜底**的权威副本：服务器不可写（国产兼容层未实现上报）时就地退化为本地进度，不阻塞播放。

**与主题系统的一致性**：海报墙、详情页、筛选栏都是**共享组件**（§7.2 的 L1），布局层只决定承载容器——A 用覆盖式抽屉、B 用检查器内 Tab、C 用全屏 overlay。三套主题不各自实现一份库视图，也不允许把“墙”写死在某个区域里。

**与渲染分流的一致性**：Direct Play 时 mpv 直接打开一个 HTTP(S) URL，`Presenter` 的分平台逻辑完全不变（Windows 仍是 `wid`+D3D11，mac/Linux 仍是 GL FBO）。转码流则不同——服务端把 HDR 转成 SDR、破坏帧精确 seek，这与 §2.5 的 HDR 承诺直接冲突，因此转码只能作为**末位回退**，且必须在 UI 明示。

### 8.2 统一适配器接口（草案）

```
MediaServerAdapter
├─ auth
│    login(Credential) -> Session { serverId, userId, token, expiresAt?, refreshable }
│    validate(Session) -> bool
│    refresh(Session) -> Session            // 仅 Plex 支持；其余返回 unsupported
│    logout(Session)
│
├─ catalog
│    listLibraries() -> Library[]
│    listItems(libraryId, kind, page, sort) -> ItemPage
│    searchItems(keyword, page) -> ItemPage
│    getItem(itemId) -> Item
│    getChildren(itemId, kind) -> Item[]
│    getPeople(itemId) -> Person[]
│    getImageRef(itemId, type, sizeTier) -> ImageRef   // 只给引用，不下载
│
├─ playback
│    getPlaybackInfo(itemId, CapabilityProfile) -> PlaybackInfo
│    getStreamTarget(itemId, sourceId, mode) -> StreamTarget  // direct | remux | transcode
│    getSubtitleTarget(itemId, sourceId, index) -> SubtitleTarget
│
├─ progress
│    reportStart(ctx) / reportProgress(ctx, positionMs) / reportStop(ctx, positionMs)
│    markPlayed(itemId) / markUnplayed(itemId)
│    getUserState(itemId) -> UserState { played, positionMs, playCount, lastPlayedAt }
│
└─ capability
     probe() -> Capability { serverKind, serverVersion, pathPrefix,
       supportsDirectFile, supportsRange, supportsProgressReport, supportsMarkPlayed,
       supportsBackdrop, supportsPeople, supportsSearch, imageResizeMode,
       idFormat, playMethodEnum, ratingScale }
```

规范化模型（上层只见这一套，不再出现各家字段）：

```
Item { sourceId, serverId, kind(movie|series|season|episode),
       title, originalTitle, year, overview, runtimeMs,
       rating, ratingSource, officialRating, genres[],
       posterRef, backdropRef, thumbRef,
       seriesId?, seasonId?, seasonNumber?, episodeNumber?,
       userState { played, positionMs, playCount, lastPlayedAt } }
```

**三套适配器覆盖五家**（极空间/飞牛复用适配器，但差异必须是**显式开关**，不是隐式假设）：

| 适配器 | 覆盖 | 关键开关 | 必须默认关闭的能力 |
|---|---|---|---|
| `EmbyFamilyAdapter`（Emby 变体） | Emby、**极空间** | `port`（Emby 8096 / 极空间 8021）、`pathPrefix`（`/emby` 优先，失败回落根路径）、`accountKind=mediaLibrary`（提示“用媒体库专用账号，不是极空间主账号”） | 自动发现（`autoDiscover=false`）、API Key 登录 |
| `EmbyFamilyAdapter`（Jellyfin 变体） | Jellyfin、**飞牛** | `port`（Jellyfin 8096 / 飞牛 8005）、`pathPrefix`（根路径优先，失败再试 `/emby`）、`accountKind=mediaAccount` | **SSDP/mDNS 自动发现强制禁用**（飞牛会广播自身为 Jellyfin，与真 Jellyfin 冲突，只能手填 `IP:8005`） |
| `PlexAdapter`（独立实现） | Plex | PIN 登录（`code` + 轮询）、`ratingKey`、`Part.key` 直链、`/:/timeline` 上报 | 不把 PIN 轮询放到 UI 线程；不假定 JWT 永久有效 |
| `FileEnumeratorAdapter`（兜底，P2） | 极空间/飞牛文件通道 | SMB / WebDAV / NFS 路径 + 本地 NFO | 不做文件名猜解式刮削默认路径 |

**无法统一、必须显式分支的字段**（降级方式写在最后一列）：

| 维度 | 差异 | 统一方式 | 降级 / 不可得时 |
|---|---|---|---|
| 条目 ID | GUID / 数字 `ratingKey` / 未知 | 只暴露不透明 `sourceId`，禁止跨服务器比较与拼接 | 无 |
| 时长与位置 | Ticks(100ns) vs 毫秒 | 适配器内统一为毫秒 | 无 |
| 媒体源 | `MediaSources[]` vs `Media/Part[]` | `PlaybackInfo.sources[{id,name,path,container,bitrate}]`，保留原始 id | 多版本时只暴露第一个可用源，其余进“更多版本”菜单 |
| 图片 | 动态 `maxWidth` vs 固定 `thumb/art` + 可选 transcode | `getImageRef(type, sizeTier)`，由适配器决定是否走转码端点 | 取图失败 → 占位图 + 标题首字母色块，不阻塞滚动 |
| 评分 | `CommunityRating/CriticRating` vs `rating/audienceRating`，量纲不一 | 保留值 + `ratingSource`，UI 标注来源，不强行归一 | 缺失则不显示评分 |
| 演职人员 | `People{Name,Role,Type}` vs `Role.tag`/`Role.role` | 统一 `{name, character, type}`，按服务器类型分支映射 | 缺失则只显示标题/简介 |
| 剧集层级 | `SeriesId/SeasonId` vs `parentRatingKey/grandparentRatingKey` | 统一 `seriesId/seasonNumber/episodeNumber` | 层级缺失时按“平铺条目”展示，不做播完自动跳集 |
| 播放方式 | 显式枚举 vs 无枚举 | 内部 `PlayMode`；Plex 由“是否走 transcode 端点”推断 | 推断失败按 `direct` 处理，失败再回退 |
| 已看语义 | 布尔 `Played` vs `viewCount` | `played` 由适配器判定（Plex `viewCount>0`） | 不可写时只读显式，标记按钮禁用并说明 |
| 分页 | `StartIndex/Limit` vs `X-Plex-Container-*` | 上层只传 `page/pageSize` | 不支持分页则一次性拉取并受条目上限约束 |
| 凭据有效期 | Plex 7 天可刷新；其余长期不可刷新；国产两家未知 | `expiresAt` 可空 + `refreshable` 标志 | 失效统一回登录页，不清空本地缓存 |

**能力探测**是配对流程的强制一步：用 6–10 个只读请求跑 `probe()`，把结果连同 `serverKind` 写进 `settings.toml` 的能力缓存。UI 只渲染能力允许的控件——不支持的排序项、已看按钮、进度条一律隐藏，而不是让用户点了再报错。

### 8.3 直链优先与转码回退

**Direct Play 是首选路径**，依据有三条：Emby 官方 Playback Guidelines 明确“客户端直接访问原文件、绕过服务器是最理想形态”；mpv 的容器/编码覆盖面远大于任何服务端转码链；只有原文件能保留 HDR、原始音轨与帧精确 seek，转码流必然降级（§2.5）。

判定顺序（**先判定，再请求**，不靠“播了失败再换”）：

| 判定项 | 依据 | 判定为“可直连”的条件 |
|---|---|---|
| 容器 | `MediaSources[].Container` / `Part.container` | MKV / MP4 / TS / M2TS / AVI / FLV 等 mpv 原生可解 |
| 视频编码 | `MediaStreams[].Codec` + profile/bit depth | H.264 / HEVC / VP9 / AV1 / MPEG-2，且 hwdec 或软解可覆盖 |
| 音频编码 | `MediaStreams[].Codec` + 声道数 | AAC / AC3 / EAC3 / DTS / TrueHD / FLAC；不支持时允许“只换音轨”的回退 |
| 字幕 | 内嵌 vs 外挂 | 内嵌 PGS/ASS 可直连；仅外挂字幕缺失时可单独取字幕端点 |
| 网络 | `supportsRange` + 实测吞吐 | 支持 206 且吞吐 ≥ 码率 1.5 倍 |

回退顺序（**严格递增，不允许跳级**）：

1. **Direct Play**：原文件字节流（Jellyfin/Emby `/Videos/{id}/stream?static=true`、Plex `Part.key`）。
2. **Direct Stream（remux）**：容器不兼容但编码都兼容时，只换封装；保留视频码流与 HDR。
3. **Transcode**：服务端 HLS；UI 明确提示“画质/HDR/seek 精度下降”，并在能力面板标记本次会话为转码。
4. **文件通道兜底**：`FileEnumeratorAdapter` 走 SMB/WebDAV/NFS 直接读路径，绕过服务端（P2）。

**Plex 的付费与权限限制必须写成能力检测**，不能等运行时 403：

| 检测项 | 请求 | 判定 | 不满足时的行为 |
|---|---|---|---|
| 本账号是否可远端播放 | `plex.tv` 账号信息 + PMS 可达性 | 服务器管理员有 Plex Pass / 本账号有 Plex Pass 或 Remote Watch Pass | 局域网内正常播放，外网播放项置灰并说明原因 |
| 是否允许下载/取原始字节 | 对 `Part.key` 发 `Range` 探测（含 `download=1`） | 200/206 即可直连 | 403 时回退 Transcode；仍失败则提示“需服务器管理员授权” |
| JWT 剩余有效期 | 本地 `expiresAt` | > 24h | 提前静默刷新（§8.4） |

同理，极空间/飞牛若 `supportsProgressReport=false`，进度条回传按钮与“已在其他设备续播”提示必须隐藏，只保留本地进度。

### 8.4 凭据与安全

- **存储位置**：所有 token / JWT / 密码只进**系统钥匙串**（Windows Credential Manager/DPAPI、macOS Keychain、Linux libsecret），键名 `palune/media/<serverId>/credential`。`settings.toml` 只存 `serverId`、地址、端口、`userId`、显示名等**非机密**字段。
- **内存与传递**：token 只存在于 `MediaLibraryService` 的会话对象；**不进入 QML**、不进日志、不进诊断包。日志对 `X-Emby-Token`、`X-Plex-Token`、`api_key` 查询参数做统一脱敏。
- **URL 里的凭据**：优先 header 鉴权；Plex 图片/直链只能走查询参数时，用**短期签名 URL**，且不写入历史记录、不进入截图/录屏外发的诊断信息。
- **7 天 JWT 的静默续期**：读取 `expiresAt`，剩余 **< 24h** 时后台刷新；刷新失败不弹窗，降级为只读（库可浏览、进度回传暂停），下一次打开应用时再试。彻底失效（401）→ 该服务器进入“需要重新登录”，其他服务器不受影响。
- **自签名证书**：默认**严格校验**，不做全局关闭。用户在配对页可对**单个服务器**选择“信任此证书”，做法是记录证书指纹（TOFU）并在该服务器的请求上放行；同时给 mpv 的 `loadfile` 传入该地址专用的 `tls-verify=no` 文件级选项，而不是改全局 `--tls-verify`。Jellyfin 10.12 起移除内置 TLS（官方推荐反代），错误页要能区分“证书不受信”“端口不通”“路径前缀错”三类。
- **多服务器隔离**：每台服务器独立 `serverId`、独立凭据、独立能力缓存、独立进度队列；适配器实例不共享可变状态；切换服务器不重登其他服务器。同一服务器多账号时按 `(serverId, userId)` 建键。

### 8.5 缓存与离线

**海报**：按档位取图，避免“下载 4K 原图当缩略图”。

| 档位 | 用途 | 请求宽 | 格式 |
|---|---|---|---|
| `thumb` | 列表/网格 | 160px | WebP 优先，JPEG 回退 |
| `poster` | 详情页 | 400px | 同上 |
| `backdrop` | 沉浸背景/详情头图 | 1280px | 同上 |

- 磁盘上限默认 **2GB**（可配 512MB–8GB），按服务器与档位分目录；LRU 淘汰，`cache/media/` 可一键清空。
- 内存位图上限 **64MB**，只保留可视区 + 预取 2 屏；滚动出屏即释放。
- 以服务器返回的 `ImageTags.Primary` / `tag` 作为版本号，tag 变化才重下，避免无效刷新。

**元数据**：`media.db` 存库列表、条目、演职人员与进度快照；失效策略按对象区分——库列表 15 分钟、条目 24 小时、`userState` 每次进入详情页实时拉取、进度上报成功后本地立即更新。支持 `ETag/If-None-Match` 的服务器走增量刷新，不支持则按 TTL 全量。

**离线/断网视图**：只渲染缓存内容，顶部横幅“离线模式，显示缓存”，隐藏依赖实时的控件（排序、搜索、刷新）。进度写入本地 `pending` 队列，网络恢复后按 §8.6 合并回传。断网**不得**阻塞启动，也不得打断正在播放的本地文件。

### 8.6 进度同步的冲突处理

| 情况 | 判定依据 | 行为 |
|---|---|---|
| 打开条目时，服务器位置与本地待回传位置不一致 | `pending.lastUpdatedAt` vs 服务器 `lastPlayedAt` / `viewOffset` | **先 flush 待回传，再读取服务器值**；flush 失败则取两者中更新的一方，绝不取较小值导致“倒退” |
| 播放中持续前进 | 本地 `time-pos` | 只上报，不读服务器；10 秒节流 + 事件立即上报 |
| 上报失败（401/403/超时/429） | 错误分类 | 401/403 → 暂停回传并标“需重新登录”；超时/429 → 指数退避（5s→30s→2min→10min，上限 24h），同一条目只保留最后一次，合并后重发 |
| 多设备同时播放同一条目 | 各设备独立 `PlaySessionId` | 服务器以最后写入为准；本地看到“另一设备正在 00:32:10 播放”这类提示**只做非阻断提示**，不暂停、不弹模态 |
| 已看判定 | 播放 ≥ 90% 或剩余 ≤ 3 分钟 | 自动 `markPlayed`；用户手动标记优先，同一账号内手动覆盖 3 次后停止自动标记 |
| 服务器不支持上报 | `Capability.supportsProgressReport=false` | 只写本地，UI 标注“该服务器不同步进度”，不产生失败重试 |
| 单位/时区/时基 | Ticks vs ms | 适配器内单点转换，规范模型只用毫秒；上报时统一取整到秒 |
| 幂等 | 重复 `reportStart` | 每次播放生成新 `PlaySessionId`；同一会话内重复上报不重复计 `PlayCount` |

原则：**服务器是跨设备共享的真值，本地是播放瞬间的真值**；两者冲突时以“时间上更新的一方”为准，且永远不做“位置倒退”的合并。媒体库条目的本地记忆键为 `(serverId, itemId)`，不得用播放 URL 或列表序号（§11.8）。

### 8.7 性能门槛（可判定）

| 指标 | 目标 | 测量方式 |
|---|---|---|
| 1000 条目库首屏 | 缓存命中 **≤ 300ms** 可交互；冷启动首次网络加载 **p50 ≤ 1.2s、p95 ≤ 2s** | 打点 + 截图，参考机 1080p |
| 海报墙滚动 | 1000 项快速滚动 **p95 UI 帧时间 ≤ 8ms**（60Hz 不掉帧），3 屏甩动丢帧 ≤ 1 | Qt frame stats + 录屏逐帧 |
| 首屏请求量 | 首屏只请求可视区 + 2 屏（≤ 60 张图）；单次列表 `pageSize ≤ 200`；只取必要 `Fields` | 网络日志计数 |
| 并发取图 | 每服务器 ≤ 6、全局 ≤ 12 并发；可视区优先，其次预取，最后是详情/背景图 | 抓包 + 线程池统计 |
| 内存 | 1000 项列表 RSS 增量 ≤ 120MB；解码位图 ≤ 64MB | RSS 采样 |
| 虚拟化约束 | 列表/网格必须用复用 delegate；只保留可视区 ± 2 屏的条目对象；图片在进入可视区前不发请求 | 代码审查 + 对象计数 |
| 断网恢复 | 恢复到可交互 ≤ 3s；离线横幅出现 ≤ 1s | 手动 + 脚本 |
| 进度上报开销 | 单次上报 p95 ≤ 80ms；1 小时播放 CPU 增量 ≤ 1% | 计时 + 采样 |

实现约束（写进代码评审清单）：取图**永远走异步队列**，禁止在 delegate 内同步 `QNetworkAccessManager`；禁止在滚动中触发 `forceLayout`；海报解码在**固定大小**的目标尺寸上做（避免滚动时重新分配）；`probe()` 与首批列表请求不得串行阻塞 UI。

### 8.8 成本增量

| 模块 | 一次性（三适配器共用） | 每家边际 |
|---|---|---|
| `MediaLibraryService`（会话、能力矩阵、进度调度、离线队列） | 8–10% | — |
| 规范化模型 + 契约测试夹具 | 4–5% | — |
| 海报墙/详情页共享组件（三主题复用，见 §7） | 10–12% | — |
| 海报缓存与虚拟化 | 5–6% | — |
| 配对向导 + 凭据/证书处理 | 4–5% | — |
| `EmbyFamilyAdapter` 首次实现 | 6–8% | +2–3%（每接一家兼容层：端口/前缀/账号类型 + 探针） |
| `PlexAdapter` | — | 6–8%（PIN、ratingKey、Part 直链、timeline、付费检测） |
| 真机验证与降级分支 | — | 2–4%/家 |

汇总：媒体库整体约为**全项目 +12–16%**，其中一次性约 **60%**、边际约 **40%**。若只做 Jellyfin + Emby（不做 Plex 与国产两家），可省约 **40%**；但“支持五家”的产品承诺与调研结论要求三套适配器一起交付。

---

## 9. 风险清单（问题 6）

| # | 风险 | 触发迹象 | 影响 | 概率 | 规避 / 缓解 | 关卡 |
|---|---|---|---|---|---|---|
| R1 | macOS OpenGL 被移除或性能不达标 | 新版系统 GL 上下文创建失败、UI 掉帧 | macOS 端整体不可用 | 中 | Presenter 收敛 GL；M0 压测；预留 AppKit/Metal B 方案；跟踪 mpv Metal 后端 | M0/M5 |
| R2 | macOS EDR 自建 layer 失败 | EDR 不激活、峰值亮度无提升、颜色发灰 | macOS HDR 缺失 | 中高 | M0 spike；失败则 `wid`+cocoa-cb 模式或只做 tone-map，并在 UI 明示 | M0 |
| R3 | macOS 公证/沙盒 | Gatekeeper 拦截、helper 无法启动、文件读取被拒 | 无法分发 / 功能缺失 | 中 | Developer ID + 公证流水线；不做 MAS 沙盒版；签名所有内嵌二进制；公证干跑 | M0/M5 |
| R4 | Wayland 合成器差异 | 黑屏、尺寸错、无 vsync、tearing | Linux 体验碎片化 | 高 | M0 覆盖 Mutter/KWin/wlroots；自建 surface 与 frame callback；保留 X11 路径 | M0/M5 |
| R5 | 硬件解码差异（厂商/驱动） | 花屏、绿屏、崩溃、seek 慢 | 播放失败率上升 | 高 | 厂商策略表 + 失败自动回退 + 本地诊断包（默认不上传，§11.11）；M3 四类 GPU 矩阵 | M3 |
| R6 | Windows DWM/覆盖窗不同步 | 拖动时 UI 与视频错位、闪烁、焦点丢失 | 体验硬伤 | 中高 | 两窗同帧更新；`HTTRANSPARENT` 命中；M2 专项测试；必要时退回 render API（无 HDR） | M2 |
| R7 | Windows HDR 管线不一致 | 灰屏、过曝、SDR 内容被当成 HDR | 画面错误 | 中 | 能力探测 + `--target-*` 显式设置；HDR 开关只对有能力的显示器出现；截图验证 | M4 |
| R8 | D3D11 render API 太新（0.42） | 崩溃、设备丢失、纹理共享失败 | 统一路径受阻 | 中 | 1.0 不用它做默认；仅 Windows `wid`；M7 再评估 | M0/M7 |
| R9 | 字幕渲染性能（复杂 ASS） | 4K 下 CPU 飙高、掉帧 | 播放不流畅 | 中 | libass 缓存与 `sub-ass-override` 策略；限制极端特效；性能预算测试 | M4 |
| R10 | mpv 版本/选项漂移 | 选项改名、行为变化 | 回归 | 高 | 锁定 0.42.x 自构建；`MpvCompat` 兼容表；启动自检；不用发行版包 | 全程 |
| R11 | 许可（GPL/LGPL/专利） | 分发被质疑、商店下架 | 法律与商业风险 | 中 | `-Dgpl=false` 构建；FFmpeg LGPL；Qt 动态链接 + 许可证；法务复核；HEVC/AV1 专利池评估 | M0/M5 |
| R12 | 崩溃隔离缺失（同进程） | 单文件导致整应用退出 | 口碑受损 | 中 | 看门狗 + 安全模式 + 自动回退；错误文件黑名单；面包屑恢复 | M2/M4 |
| R13 | 音频直通/独占平台差异 | 无声、声道错、爆音 | 影音体验受损 | 中 | 设备能力探测；失败回退 PCM；HDMI 声道白名单；mac 直通单独评估 | M3 |
| R14 | 网络流与 TLS/代理 | 加载失败、证书错误 | 功能不可用 | 中 | 统一错误映射；代理设置；超时与重试；明确“不支持 DRM” | M3 |
| R15 | 打包与自动更新 | 更新失败、签名失效、回滚困难 | 发布事故 | 中 | 分阶段灰度；签名校验；N→N+1 与回滚演练；离线安装包 | M5 |
| R16 | 性能预算失守（4K60 HDR） | 掉帧、风扇狂转、功耗高 | 达不到 1.0 性能门槛 | 中 | M0/M4 硬门槛；GPU 厂商矩阵；必要时限制 shader/降级缩放 | M0/M4 |
| R17 | 多显示器/DPI/热插拔 | 窗口错位、模糊、全屏跑到错误屏 | 常见投诉 | 高 | 物理像素布局；监听屏变更；全屏按 `screen` 绑定；三平台自动化用例 | M2/M5 |
| R18 | 插件/脚本安全 | 恶意脚本读文件、外联 | 安全事件 | 低–中 | 默认关闭 L1；L2 用 WASM 能力白名单；不暴露裸 handle | M7 |
| R19 | 三主题布局回归/状态丢失 | 切换后列表滚动/焦点/面板宽度丢失；B/C 布局错位 | 体验受损、维护成本失控 | 高 | 状态仓 + 区域契约 + 每主题截图基线；涉及组件的 PR 必须跑三主题冒烟 | M1/M2 |
| R20 | 切主题导致播放中断/黑帧 | 切换时出现 `loadfile`、render context 重建、帧空洞 | 严重体验事故 | 中 | `VideoHost` 置于布局树外，只重绑矩形；切换路径禁止调用播放器；自动化断言 §7.3 | M1/M2 |
| R21 | 极空间/飞牛兼容层端点不完整 | 认证通但列库/取图/直连/上报 4xx、字段缺失 | 国产两家“能登录不能用” | 高 | 配对强制 `probe()` 写能力矩阵；缺项隐藏控件并降级到 SMB/WebDAV 文件通道 | M6 |
| R22 | 国产两家 token 生命周期与 HTTPS 未公开 | 使用 24–72h 后突然 401；8021/8005 无 TLS | 长期会话中断、外网不可用 | 中高 | 自动重登 + 凭据本地持久化；外网只支持反代/端口转发并在配对页提示 | M6 |
| R23 | Plex 付费墙与共享用户下载权限 | 外网播放置灰、`Part.key` 返回 403 | 部分用户无法播放/无法续播 | 中高 | 配对期检测 Plex Pass / Remote Watch Pass / 下载权限；403 回退转码流 | M6 |
| R24 | 进度冲突导致位置倒退或多设备互踩 | 换设备续播回到更早位置、`PlayCount` 虚增 | 用户数据受损 | 中高 | 先 flush 待回传再读；只取更新的一方；`PlaySessionId` 幂等；上报队列合并 | M6 |
| R25 | 海报墙大数据量卡顿/内存失控 | 1000+ 条目滚动掉帧、RSS 飙升 | 核心体验崩坏 | 中高 | 复用 delegate + 可视区 ±2 屏 + 并发/位图上限 + 档位图（§8.7） | M6/M7 |
| R26 | 名称商标冲突 | 第 9 类检索尚未完成；发布后被异议或被迫改名 | 品牌资产与已发布标识作废 | 中高 | 1.0 前完成 CN/US/EU 专业检索；尽早注册 `palune.app`/`palune.dev` 与包名；标识在公测前冻结（§5.10） | M7 |

**Top 3 会翻车的点**：R4（Wayland 碎片化）、R6（Windows 覆盖窗同步）、R2（macOS HDR）。这三项都设为 M0/M2 的“不通过就不进入下一阶段”的硬门；R20（切换断播）作为 M1/M2 的额外红线，R24（进度倒退）作为媒体库阶段的红线。

---

## 10. 待拍板事项

1. macOS 最低系统版本：13 还是 14（影响 `CADisplayLink` 与 EDR 实现）。
2. Windows 是否接受“双窗口”形态（覆盖窗方案）；若不接受，则 1.0 放弃 HDR，统一 render API。
3. 是否购买 Qt 商业许可（换取静态链接与官方支持）；默认按 LGPL 动态链接推进。
4. 录制功能是否 1.0 就要（转码录制需要 ffmpeg 子进程与硬件编码器矩阵）。
5. 是否提供任何形式的使用统计/遥测（建议：**1.0 不提供**，只保留本地诊断与用户手动导出的诊断包，§11.11）。
6. 3D/VR 是否彻底砍掉（建议 1.x 不做，仅在关于页说明）。
7. 内置主题开放到什么程度的用户偏好（建议：只开放强调色、字号档、密度、面板宽度、紧凑断点这类令牌级覆盖；**不开放控件结构、不开放主题包格式**，§11.3）。
8. 媒体库是否 1.0 必须交付（建议：作为独立阶段 M6 交付；若延期，1.0 不得对外宣称“支持五家”）。
9. 是否要求用户提供服务器 API Key（建议：不要求，统一走用户登录；API Key 仅作为高级选项，且提示其等同管理员权限）。
10. 是否允许服务端转码（建议：允许，但只作末位回退，且在 UI 明示画质/ HDR / seek 精度下降）。
11. 是否为极空间/飞牛做 TMDB/TVDB 元数据兜底（建议 P2，与文件通道一起做，避免把识别率承诺写进 1.0）。

---

## 11. 评审结论与采纳条款

> 本节为**正式约束**，不是建议。每条均已回填到对应章节；与正文其他段落冲突时，以本节为准。偏离这些条款需要 ADR 与签字，不允许在实现阶段“先做了再说”。

### 11.1 范围收敛：不以“大而全功能清单”为验收基线

- **1.0 的 P0 只有十项**：播放、字幕、音轨、硬解、变速、截图、快捷键、记忆位置、下一集、全屏。验收矩阵按这十项组织，不按功能条数组织。
- 十项之外的能力（章节、书签、A-B 循环、滤镜链、自定义着色器、录制、缩略图预览、置顶/迷你窗、媒体服务器、自动更新等）一律降为 **P1/P2**，不得占用 P0 的验收与排期。
- **明确排除在 1.0 之外**：
  - 3D / VR（含 SBS/OU 转红蓝、头显）：mpv 原生 stereo 已移除，只剩 lavfi 近似，投入高、受众窄。
  - 位图皮肤 / 第三方皮肤 / 私有二进制皮肤格式：见 §11.3，跨平台无对应物。
  - 软解滤镜矩阵（多套去隔行/降噪/超分预设组合）：组合爆炸，回归成本高于收益。
  - BD-J 菜单、DRM 流、电视卡/DVB、在线字幕搜索与翻译、内置刮削建库。
- 回填：§4 功能表按此重新定级；§4.8 给出十项的对应能力与排除清单。

### 11.2 记忆与手感优先（P0 质量项）

以下五项与十项功能同等重要，**没有达标即视为 P0 未完成**：

| 质量项 | 指标（1.0 门槛） | 测量方式 |
|---|---|---|
| 记忆位置 | 正常关闭与强杀各 100 次，恢复到 ±1s 的成功率 **100%**；跨版本升级不丢 | 自动化脚本 + 强杀演练 |
| 记忆音量 / 字幕轨 / 倍速 | 重开后复原率 **100%**；单文件覆盖值优先于全局默认 | 状态快照对比 |
| 记忆窗口几何 | 位置/大小/最大化/全屏状态复原率 100%，且落在可用屏幕内 | 多显示器快照 |
| 拖动跟手 | 拖动进度条到画面更新 **p95 ≤ 50ms**；松手到画面稳定 **p95 ≤ 250ms** | 高速摄像机/帧时间戳 |
| 精确 seek | 目标帧命中率 **100%**（±0 帧）；1080p p95 ≤ 200ms、4K p95 ≤ 350ms | 帧号比对脚本 |

回填：§4.4 的 seek 行、§5.5 的播放状态与窗口状态、MILESTONES M3 验收。

### 11.3 不做皮肤系统

- 界面**只支持 A/B/C 三套内置主题**（§6/§7）。**不支持第三方皮肤、不支持位图皮肤、不支持 QSS/CSS 覆盖、不开放主题包格式**。
- 理由：位图皮肤依赖固定的窗口装饰与 DPI，Windows/macOS/Linux 没有对应物；三平台各自的窗口管理、缩放与无障碍要求会让皮肤在不同平台上失真，维护成本无上限。
- 允许的定制只有“内置主题内的用户偏好”（如强调色、字号档、面板宽度、紧凑断点），这些走设计令牌覆盖，**不改变控件结构**。
- 回填：§4.7 删除“用户自制主题包”条目；§10 拍板项相应移除；MILESTONES M7 的 P2 清单同步删除。

### 11.4 字幕双轨：ASS 走 libass，SRT 自行排版

- **边界**：
  - ASS/SSA（含特效、定位、卡拉 OK、多图层）→ **完全交给 libass**，应用不解析、不重排，只传样式覆盖选项。
  - SRT（纯文本 + 简单时序）→ **应用侧自行排版渲染**：解析、断行、双向文本、描边/阴影、安全区、字号缩放都由我们控制，以 mpv 的 `time-pos` 驱动。
  - 混排（双字幕）时层级固定：ASS 在下、SRT 在上；两者不得互相覆盖样式。
- **共用部分**：字体发现与加载（fontconfig / CoreText / DirectWrite 统一封装）、样式令牌（字号档、颜色、描边、安全区比例）、渲染时序（统一按 `video-params` 的 OSD 分辨率与像素比换算，提前一帧调度）、字幕延迟与轨切换逻辑。
- **为什么不全走 libass**：libass 会把 SRT 转成默认 ASS 样式，字号与安全区无法跟随设计令牌与窗口尺寸；render API 下字幕被烧进视频帧，改字号/位置就等于重编码一帧，主题切换后无法即时生效；libass 的 CJK 字体回退在各平台不一致，会直接破坏排版质量。
- **代价（必须接受）**：SRT 自绘需要自己做换行、双向文本、坐标缩放与截图合成；`window` 模式截图必须把自绘字幕一并合成，纯视频截图则不得含字幕。这条要在 M3 的截图验收里体现。
- 回填：§4.3 字幕表与新增 §4.3.1；MILESTONES M3 的字幕与截图条目。

### 11.5 HDR 立场：SDR 是基线，HDR 是“可检测、可直通、不可直通时明确降级”

- **SDR 是 1.0 的质量基线**：Rec.709 / sRGB 输出，tone mapping 后与参考帧 ΔE2000 ≤ 3。任何平台、任何 GPU，SDR 都必须正确。
- HDR 只有三种状态，且**必须在 UI 可见**（设置页与播放信息面板同时显示）：
  1. `HDR 直通`：原生 HDR10/HLG 输出，交换链/图层色彩空间已设置。
  2. `HDR 已检测，映射到 SDR`：源是 HDR，但链路不支持，走 libplacebo tone mapping，并明确标注。
  3. `SDR`：源与链路都是 SDR。
- **禁止**用错误的 tone mapping、假 PQ 标记或“设置里打开 HDR 开关”冒充支持。状态由**实测能力探测**决定，不由用户开关决定；探测失败一律落到第 2 或第 3 态。
- 回填：§2.5 管线表后的结论段；§4.1 HDR 行；MILESTONES M4 的 HDR 矩阵与状态显示验收。

### 11.6 Windows 双窗实测项（写进 M0 必测与判定标准）

Windows 的“视频窗 + 覆盖窗”不是设计假设，必须在 M0 用数据判定：

| 实测项 | 方法 | 判定标准 |
|---|---|---|
| 全屏切换 | 窗口态↔全屏连续切换 20 次，录屏逐帧 | 两窗偏移 ≤ 1 帧；无 > 100ms 黑帧；无闪烁；切换后控件命中率 100% |
| 多显示器不同 DPI 拖动 | 100%↔150%↔200% 显示器间拖动 10 次，含跨屏中途松手 | 偏移 ≤ 1 帧；无错位/模糊；`WM_DPICHANGED` 后交换链按物理像素重建；点击命中 100% |
| Alt-Tab / 任务栏 | 逐项检查 | 只出现一个应用窗口；恢复后两窗 z-order 正确 |
| Snap / 最小化恢复 | Windows  Snap 布局 4 种 + 最小化/恢复 10 次 | 视频与 UI 不错位；无黑帧 |

任一项 FAIL → 按 §2.4 的备用路径改写 ADR-001，不允许带病进入 M1。

### 11.7 libmpv 动态链接是硬约束

- libmpv **必须动态链接**：`-Dgpl=false`、`BUILD_SHARED_LIBS=ON`。**禁止静态链接**（`BUILD_SHARED_LIBS=OFF`）与 `-Dgpl=true` 构建。
- 理由：LGPLv2+ 要求允许用户替换库；静态链接会触发“提供目标文件/源码”的义务，闭源分发不可接受。Qt 同理，必须动态链接并随包提供许可证与源码获取方式。
- macOS 额外工序：每个内嵌 `.dylib`/`.framework` 单独签名，`install_name` 统一重写为 `@rpath`，签名顺序必须在主程序之前，公证与 stapling 在全部签名完成后执行。这一套从 M1 的第一份内测包就要跑通，不能等 M5。
- Windows/Linux：DLL/SO 与主程序同目录或受控 `rpath`；禁止 `LD_PRELOAD` 式替换破坏签名链。
- 回填：§3.4 许可条目、§5.8 发布流程、MILESTONES M1 的构建验收。

### 11.8 进度记忆键规则

- 本地文件：`contentKey = sha256(规范化绝对路径)`（Windows 统一小写盘符、正斜杠、去尾随分隔符），取前 128 位作为键。**禁止**使用播放列表序号、数组下标、显示标题或文件名（无扩展名）作为键。
- 文件被移动/重命名时以“同大小 + 同 mtime + 同前 64KB 哈希”做一次弱匹配迁移，命中才迁移，避免误合并。
- 网络源单列规则：
  - 直链（HTTP/HLS 等）：`sha256(规范化 URL)`，**剔除**签名参数与临时 token（`token`、`sig`、`expires`、`X-Plex-Token` 等），避免每次签名变化产生新键。
  - 媒体库条目：键为 `(serverId, itemId)`，**不使用播放 URL**——转码与直连的 URL 不同，但必须是同一条进度。
- 键不可用时（如无法规范化）→ 不写记忆并记诊断日志，不得退化为序号。
- 回填：§5.5 持久化章节、§8.6 进度同步。

### 11.9 快捷键分平台

- 绑定表**按平台分列**（Windows / macOS / Linux），不允许用“一套默认键位 + 运行时 if”糊过去；`Cmd` 与 `Ctrl`、`Option` 与 `Alt` 的语义差异必须体现在表里。
- 启动时必须做**冲突检测**：同一作用域内同一手势绑定两个以上动作 → 拒绝启动默认绑定，并输出具体冲突项（动作 ID、键位、平台、作用域）。
- 冲突信息必须可见：设置页“快捷键”页顶部列出冲突，可一键定位与解绑；日志同步记录。
- 媒体键与全局热键单独一个作用域，允许与普通快捷键重复（系统级优先），但要在冲突面板里标注“系统级”。
- 回填：§4.7 快捷键行、MILESTONES M2 第 3 条。

### 11.10 窗口状态

- 持久化：位置、大小、最大化/全屏状态、置顶开关、全屏时是否隐藏鼠标（延迟与阈值可配）。
- 显示器拓扑变化（拔插、分辨率/DPI 变化、主屏切换）后，恢复的窗口必须**回落到当前可用屏幕内**（完整可见，标题栏不被遮挡），并按该屏 DPI 重新计算几何。
- 若原显示器消失，窗口移动到主屏并居中，保持宽高比；不得出现窗口在屏幕外“不可找回”。
- 回填：§5.5 配置表（`[ui.window]`）、§6.4 平台差异、MILESTONES M2 第 8 条窗口行为清单。

### 11.11 崩溃 dump 本地留存、不上传

- 崩溃转储（minidump）、面包屑、日志**只存本地**，默认**不上传**、不建后台遥测通道。
- 本地保留最近 **5 份**转储（滚动覆盖），超过 30 天自动清理；路径在诊断页可见。
- 提供**手动发送入口**：用户主动勾选并导出诊断包（默认脱敏：媒体路径、标题、服务器地址、token 全部打码），由用户自行发送给支持渠道。
- 隐私政策必须写明“默认不上传任何崩溃或使用数据”；任何未来的遥测都必须 opt-in 且单独开关。
- 回填：§5.8 崩溃恢复、§9 风险 R5/R12 的遥测表述、MILESTONES M5 隐私项。

### 11.12 更新签名从第一阶段起建

- 签名与更新通道在 **M1 建立**，不是 M5 补：Windows EV/OV 证书与安装器签名、macOS Developer ID + Hardened Runtime、Linux 包签名与仓库元数据签名。命名与标识按 §5.10 固定（`app.palune.player`）；更新源设计标识为 `updates.palune.app`，**当前实际走源码仓 GitHub Release**（§5.10 已注明现状），域名落地前不得把两者混写。
- 更新包必须带签名校验，客户端**拒绝安装未签名或签名不匹配的更新**；更新通道分 stable/beta 两个 feed，公钥内置在客户端（禁止运行时下载公钥）。
- 从 M1 起，每一份内测包都必须可验签；未签名构建不得进入内测分发。
- 回填：MILESTONES M1 完成标志新增一条；§5.8 发布流程；§9 风险 R15。

### 11.13 性能基线

| 项 | 1.0 门槛 | 说明 |
|---|---|---|
| 4K60 HDR 硬解 | 掉帧 ≤ 0.01%，音频欠载 0 | Windows 参考机；HDR 直通或明确映射 |
| UI 帧率 | 播放中稳定 **60fps**（UI 帧时间 p95 ≤ 16.7ms），无掉帧 | 与主题无关；B 的常驻面板另测空闲 GPU ≤ 5% |
| 冷启动到出画 | **≤ 1.5s**（本地 1080p，参考机），内部目标 ≤ 800ms | 打点从进程启动到首帧呈现 |
| 8K | **明确不承诺**。只做能力探测与提示，不列入验收 | 写进支持矩阵，不写进保证 |

- 回填：§1.3 质量基线表；MILESTONES M4 第 1 条与 M0 的启动判定。

---

## 12. 依据

- libmpv `render.h` / `render_d3d11.h` / `client.h`（渲染后端、线程约束、`wid` 平台范围、GPL 构建说明）。
- mpv 手册：`--hwdec`、`--wid`、`--target-colorspace-hint`、`--d3d11-output-csp`、`--d3d11-output-mode`、`--audio-exclusive`、`--audio-spdif`、`--secondary-sid`、`--sub-codepage`、`--stream-record`、`--screenshot-*`、`--gpu-context`。
- 媒体服务器可行性调研：《媒体服务器接入可行性调研》（`MEDIA_SERVER_FEASIBILITY.md`，与本文档并行的调研产出，含各家官方端点、账号类型、端口、直连与进度上报证据，以及必须真机实测的探针清单）。
- 里程碑与验收：[MILESTONES.md](./MILESTONES.md)。
