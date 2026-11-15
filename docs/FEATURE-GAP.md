# 功能面缺口清单（FEATURE-GAP）

- 盘点对象：Palune 1.0.0-RC1（Qt 6.8.3，C++ 内核 + QML 界面，播放后端为动态加载的 libmpv）。
- 对照基线：VLC、mpv 本体及其前端、MPC 系列、SMPlayer 这类主流桌面视频播放器的通用能力面。
- 本轮只出清单，不改代码。
- 取证口径：**有入口不等于可用**。每条给实现路径（文件 / 类 / 函数 / mpv 属性写入）或"全仓无该属性写入、无该 UI 链路"的反证；只有页签、按钮但没有数据链路的，一律判"有但残缺"。
- 本文引用以"文件 + 符号"为准，不写易漂移的行号。
- 标注说明：
  - 状态四态：`已有且可用` / `有但残缺` / `没有` / `不该做`。
  - 优先级：P0 影响日常使用（缺了会被认为不完整）、P1 进阶但常见、P2 小众；`已有且可用` 条目给的是该能力的固有重要度，不代表存在缺口。
  - 成本：小 / 中 / 大；`已有且可用` 记为"—（已实现）"。

---

## 0. 汇总

| 功能域 | 已有且可用 | 有但残缺 | 没有 | 不该做 | 其中 P0 缺口 |
|---|---:|---:|---:|---:|---:|
| 1 播放核心 | 6 | 1 | 4 | 0 | 1 |
| 2 播放控制 | 3 | 2 | 3 | 0 | 1 |
| 3 字幕 | 5 | 2 | 2 | 0 | 2 |
| 4 音频 | 3 | 1 | 2 | 0 | 0 |
| 5 画面 | 0 | 0 | 3 | 1 | 1 |
| 6 媒体库与片单 | 3 | 0 | 2 | 0 | 1 |
| 7 窗口与交互 | 3 | 3 | 2 | 0 | 1 |
| 8 快捷键与遥控 | 1 | 0 | 2 | 1 | 0 |
| 9 网络播放 | 1 | 2 | 1 | 0 | 1 |
| **合计** | **25** | **11** | **21** | **2** | **8** |

P0 缺口共 8 条：**片单/播放列表 2 条**（队列 + 拖放）、**字幕 2 条**（在线搜索 + 字体样式）、**画面相关 2 条**（比例 + 截图），其余为章节、网络地址入口各 1 条。P1 缺口 18 条，P2 缺口 6 条，另有 2 条"不该做"。

---

## 1. 播放核心（解码 / 硬解 / HDR / 音轨 / 比例 / 旋转裁切 / 直通 / 变速）

| 条目 | 状态 | 判定依据（文件 / 行为） | 优先级 | 成本 |
|---|---|---|---|---|
| 视频解码与容器覆盖 | 已有且可用 | `src/mpv/MpvClient.cpp` 的 `applyOptions` 以 `vo=gpu-next` 初始化，`openFile` 走 mpv `loadfile`，解码全部交给 libmpv；既有行为证据 `docs/evidence/m1-smoke.json` 20 样本 20/20。 | P0 | —（已实现） |
| 硬件解码 | 已有且可用 | `applyOptions` 在 Windows 默认写 `hwdec=d3d11va`；`MpvClient::setHwdecMode` 按设置写 `hwdec`；技术信息读 `hwdec-current` 展示。设置页提供 auto/hardware/off。 | P0 | —（已实现） |
| HDR 播放（检测 + 色调映射） | 有但残缺 | 已有：`MpvClient::setToneMapping` 写 `tone-mapping=bt.2390` 与 `target-peak`，并按 `video-params` 的 gamma/primaries 识别 HDR10/HLG。缺：未写 `target-colorspace-hint`、`hdr-compute-peak`，Windows 下不会请求系统进入 HDR 输出，HDR 内容按 SDR 目标做色调映射；无峰值/色域手动覆盖；HDR 直通无实机证据。 | P1 | 中 |
| 音轨切换 | 已有且可用 | `MpvClient` 解析 `track-list` 生成音轨列表，`setAudioTrack` 写 `aid`；快捷键 `A` 走 `ActionRegistry`，设置页可点选；界面轨道名来自 `track-list` 的 title/language，不是写死占位。 | P0 | —（已实现） |
| 画面比例（16:9 / 4:3 / 原始 / 填充 / 自定义） | 没有 | 全仓未命中 `video-aspect` / `video-aspect-override` / `panscan` 写入；`ActionRegistry` 的动作表与设置页均无比例项。 | P0 | 小 |
| 旋转（90° 步进 / 水平镜像） | 没有 | 全仓未命中 `video-rotate` / `video-flip` 写入；QML 无入口。 | P1 | 小 |
| 裁切（黑边 / 自定义区域） | 没有 | 全仓未命中 `video-crop` 写入；媒体库缩略图模块的裁切只作用于缩略图，与播放画面无关。 | P2 | 中 |
| 音频直通（AC3/DTS/TrueHD → HDMI） | 没有 | 全仓未命中 `audio-spdif` 写入；音频设置只有输出设备、声道、延迟三项。 | P1 | 小 |
| 变速不变调 | 已有且可用 | `MpvClient::setSpeed` 写 mpv `speed`，快捷键与设置页均可调；mpv 默认 `audio-pitch-correction=yes`，应用未关闭，变速走音高校正。局限：没有独立"保持音调"开关，也没有细档位。 | P1 | —（已实现） |
| 去隔行 | 已有且可用 | `MpvClient::setDeinterlace` 写 `deinterlace=auto/yes/no`，设置页有入口并落盘。 | P2 | —（已实现） |
| 网络缓存时长 | 已有且可用 | `MpvClient::setCacheSeconds` 写 `cache` 与 `cache-secs`，设置页可选预置档位。 | P2 | —（已实现） |

## 2. 播放控制（逐帧 / AB 循环 / 书签章节 / 精细 seek / 跳片头尾）

| 条目 | 状态 | 判定依据（文件 / 行为） | 优先级 | 成本 |
|---|---|---|---|---|
| 逐帧前进 / 后退 | 已有且可用 | `MpvClient::frameStep(bool)` 调 mpv `frame-step` / `frame-back-step`；动作表绑定 `F` / `Shift+F`。 | P1 | —（已实现） |
| 精细 seek（±1s / ±10s / ±60s / 拖拽） | 已有且可用 | `MpvClient::seek` 使用 `absolute+exact`；`WindowController::action` 提供三档步长；`qml/components/SeekBar.qml` 支持拖拽定位。 | P0 | —（已实现） |
| A-B 循环 | 没有 | 全仓未命中 `ab-loop-a` / `ab-loop-b` 写入，无 AB 标记 UI 与快捷键。 | P1 | 小 |
| 循环播放（单文件 / 列表） | 有但残缺 | `MpvClient::setLoopFile` 存在，但注释写明"长时压测用"，只写 `loop-file=inf`；无动作 ID、无快捷键、无设置项；"列表循环"依赖播放列表，而应用没有播放列表（见 6.5）。 | P1 | 小 |
| 书签 | 没有 | 全仓无书签读写；`Settings` 只持久化窗口几何、音量、速度、续播位置等，没有书签容器。 | P2 | 中 |
| 章节（列出 + 跳转） | 有但残缺 | 只有页签：`qml/layouts/LayoutB/LayoutB.qml` 的"章节"入口下写死"当前文件没有章节信息"，未绑定 `chapter-list`；`MpvClient` 不读章节，`ActionRegistry` 无章节动作，A/C 布局没有入口。 | P0 | 小 |
| 跳片头 / 片尾 | 没有 | 无片头尾时间点配置、无自动跳过逻辑；`WindowController` 的续播只处理"上次播放位置"（`m_pendingResume` / `resumePosition`）。 | P1 | 中 |
| 上一集 / 下一集 | 已有且可用 | `WindowController::playSibling(±1)` 按文件名排序取同目录相邻媒体，绑定 `N` / `P`；播放结束后的自动下一集可开（`endFile` 路径）。局限：不是播放列表语义，跨目录无效。 | P1 | —（已实现） |

## 3. 字幕（在线搜索 / 整体位移 / 双字幕 / 字体样式 / ASS）

| 条目 | 状态 | 判定依据（文件 / 行为） | 优先级 | 成本 |
|---|---|---|---|---|
| 外挂字幕加载 | 已有且可用 | `MpvClient::addSubtitleFile` 调 `sub-add <file> select`；设置页"选择字幕文件"调用它。 | P0 | —（已实现） |
| 字幕轨切换 / 关闭 | 已有且可用 | `MpvClient::setSubtitleTrack` 写 `sid`（-1 关闭）；`V` / `Shift+V` 走动作表；设置页列出轨道并可点选。 | P0 | —（已实现） |
| 在线字幕搜索 / 下载 | 没有 | 全仓无字幕站点 API、无搜索 UI、无下载目录与落盘逻辑；字幕设置组只有轨道、样式、延迟、位置、编码、外挂文件。 | P0 | 大 |
| 时间轴整体位移 | 已有且可用 | `MpvClient::setSubtitleDelayAbsolute` 读写 `sub-delay`；`Shift+Z` / `Shift+X` 微调；设置页步进器与播放中调节共用同一属性。 | P0 | —（已实现） |
| 双字幕（主 / 次） | 没有 | 全仓未命中 `secondary-sid` / `secondary-sub-*` 写入，UI 无双字幕选择。 | P1 | 中 |
| 字体与样式 | 有但残缺 | 已有：字号、颜色、描边、垂直位置，经 `MpvClient::setSubtitleStyle` 写 `sub-font-size` / `sub-color` / `sub-border-size` / `sub-pos`。缺：字体族、字重、背景框、阴影、行距、左右边距；没有 `sub-fonts-dir` 配置。 | P0 | 小 |
| ASS 特效支持度 | 有但残缺 | 字幕由 libmpv 绘制在视频窗（设置页的样式块只是预览，不是实际渲染链路），ASS 效果取决于随包 `third_party/libmpv/libmpv-2.dll` 内置的 libass。应用未写 `sub-ass-override` / `sub-ass-vsfilter-*`，无法强制或关闭 ASS 样式；仓库内无 ASS 特效实机证据。 | P1 | 小 |
| 字幕编码 | 已有且可用 | `MpvClient::setSubtitleEncoding` 写 `sub-codepage`（auto / utf-8 / gb18030），设置页可选并落盘。 | P2 | —（已实现） |
| 同名字幕自动加载 | 已有且可用 | `MpvClient::setSubtitleAutoLoad` 写 `sub-auto=fuzzy/no`，设置开关落盘。 | P2 | —（已实现） |

## 4. 音频（均衡器 / 声道映射 / 音量归一化）

| 条目 | 状态 | 判定依据（文件 / 行为） | 优先级 | 成本 |
|---|---|---|---|---|
| 音量 / 静音 | 已有且可用 | `MpvClient` 读写 `volume` / `mute`；`M` 与方向键走动作表；退出时写回配置。 | P0 | —（已实现） |
| 输出设备选择 | 已有且可用 | `MpvClient::setAudioDevice` 写 `audio-device`，设备列表来自 `audio-device-list`；设置页下拉可选。 | P1 | —（已实现） |
| 音频延迟 | 已有且可用 | `MpvClient::setAudioDelay` 写 `audio-delay`，设置页步进器可调。 | P1 | —（已实现） |
| 均衡器 | 没有 | 全仓未命中 `af=equalizer` / `superequalizer` 等滤镜写入，无频段 UI 与预设。 | P1 | 中 |
| 声道映射 | 有但残缺 | 只有 `MpvClient::setAudioChannels` 的 auto/stereo/5.1 三选一。缺：自定义下混/上混、单声道/2.1/7.1、逐声道测试音。 | P1 | 中 |
| 音量归一化 / 夜间模式 | 没有 | 全仓未命中 `dynaudnorm` / `loudnorm` / 动态范围压缩相关写入。 | P1 | 小 |

## 5. 画面（色彩管理 / 锐化降噪 / 截图录像 / 旋转）

| 条目 | 状态 | 判定依据（文件 / 行为） | 优先级 | 成本 |
|---|---|---|---|---|
| 色彩管理（ICC / 色域 / gamma / 亮度对比度） | 没有 | 只有 1.3 的 `tone-mapping` / `target-peak`；全仓未命中 `icc-profile*`、`target-prim`、`target-trc`、`brightness` / `contrast` / `saturation` 写入。 | P2 | 大 |
| 锐化 / 降噪 | 没有 | 无 `vf`、自定义 shader、`deband` 设置；缩放算法（`scale` / `dscale`）也未暴露。 | P2 | 中 |
| 截图 | 没有 | 仅设了 `screenshot-high-bit-depth=yes`；`screenshot-raw` 只用于自测亮度取样。无 `screenshot-to-file` 调用、无快捷键、无保存目录设置。 | P0 | 小 |
| 录像 / 片段导出 | 不该做 | 现有内核只通过 libmpv 解码与显示，libmpv 不提供封装/编码接口；要做录像需另起一套 FFmpeg 编码管线与音视频复用，与"只依赖随包 libmpv、内核只做播放"的既有约束冲突，投入与收益不匹配。 | P2 | 大 |
| 旋转 | 没有 | 同 1.6，不重复计数。 | P1 | 小 |

## 6. 媒体库与片单

| 条目 | 状态 | 判定依据（文件 / 行为） | 优先级 | 成本 |
|---|---|---|---|---|
| 本地目录扫描 / 索引 / 缩略图 | 已有且可用 | `src/media/MediaLibraryService` 提供 `addRoot` / `rescan` / `rebuildLibraryIndex`，索引持久化；`ThumbnailProvider` 生成缩略图并在界面使用。 | P0 | —（已实现） |
| 媒体服务器（Jellyfin / Plex 协议族） | 已有且可用 | `MediaLibraryService::connectServer` 支持密码与 API Key 两种接入；`JellyfinAdapter` / `PlexAdapter` 负责目录与播放地址；播放进度回写媒体服务器（`MediaLibraryService` 的进度接口被 `WindowController` 调用）。 | P1 | —（已实现） |
| 海报墙 / 列表双视图 | 已有且可用 | `MediaLibraryService` 暴露 `viewMode`（poster/table），`qml/LibraryPanel.qml` 两套 delegate 实际切换。 | P0 | —（已实现） |
| 搜索 / 排序 / 筛选 | 没有 | `qml/LibraryPanel.qml` 无搜索框、无排序控件、无类型/年份筛选；`MediaLibraryService` 也没有查询接口暴露给界面。 | P1 | 中 |
| 播放列表 / 队列（创建、追加、排序、保存） | 没有 | `LayoutB.qml` 的"队列"页签内容实际是"最近打开"（`WindowController::recentFiles`），不是可编辑队列；`MpvClient::openFile` 只 `loadfile replace`，无 `playlist-*` 属性/命令；无播放列表模型与持久化。 | P0 | 大 |

## 7. 窗口与交互（置顶 / 迷你 / 全屏 / 多实例 / 拖放 / 外部调起）

| 条目 | 状态 | 判定依据（文件 / 行为） | 优先级 | 成本 |
|---|---|---|---|---|
| 窗口置顶 | 没有 | `src/window/OverlayWindow.cpp` 的窗口 flags 只有无边框/无阴影相关项，没有 `WindowStaysOnTopHint`；无动作 ID 与设置项。 | P1 | 小 |
| 迷你模式 | 没有 | `src/ui/CompactController` 是窄窗断点布局（默认 560px 断点 + 滞回 + 防抖），改变的是同一窗口内的布局密度，不是独立迷你窗/悬浮小窗；无"迷你模式"动作与独立几何记忆。 | P1 | 中 |
| 全屏控制 | 已有且可用 | `WindowController::toggleFullscreen` 绑定 `F11` 与双击；`qml/Main.qml` 有播放中控制层自动隐藏、悬停恢复（`chromeVisible` + 隐藏计时器）。 | P0 | —（已实现） |
| 多实例 | 有但残缺 | 无 `QLocalServer` / `QSharedMemory` / 单实例锁，二次启动即第二个进程与第二套 mpv；两个实例共写同一 `settings.toml`，无进程间互斥或仲裁；也没有"新参数转发给已运行实例"的机制。 | P1 | 中 |
| 播放列表拖放 | 有但残缺 | `qml/Main.qml` 的 `DropArea` 只取 `drop.urls[0]` 调 `openFile`：多选文件只播第一个、替换当前播放、不能追加到队列、不能拖入字幕文件。 | P0 | 小 |
| 外部程序 / 协议调起 | 有但残缺 | 已有：位置参数与 `--open`、`palune://play?url=` 解析；Windows 注册 `HKCU\Software\Classes\palune`。缺：Windows 未注册视频文件类型关联；macOS `packaging/macos/Info.plist.in` 只见 `CFBundleURLTypes`、未见 `CFBundleDocumentTypes`，双击视频不能进本应用。 | P1 | 小 |
| 托盘 | 已有且可用 | `src/app/AppController.cpp` 创建 `QSystemTrayIcon` 与显示/退出菜单；关闭/最小化到托盘可配（`Preferences` 对应开关）。 | P1 | —（已实现） |
| 无边框拖拽 / 四边缩放 / 系统贴边 | 已有且可用 | `WindowController` 的 `startSystemMove` / `startSystemResize` 交给系统；Windows 保留 `WS_THICKFRAME` 以支持 Snap；行为证据 `docs/evidence/window-drag-resize.json`。 | P0 | —（已实现） |

## 8. 快捷键与遥控

| 条目 | 状态 | 判定依据（文件 / 行为） | 优先级 | 成本 |
|---|---|---|---|---|
| 快捷键自定义 / 冲突检测 / 持久化 | 已有且可用 | `src/ui/ActionRegistry.cpp` 维护 40 条动作表，`conflicts()` 检测同 scope 冲突；设置页改键/解绑/重置，`Preferences` 覆盖写回 `settings.toml`；`handleKey` 由主窗键盘事件调用。 | P0 | —（已实现） |
| 全局媒体键（键盘播放/暂停键） | 没有 | 键盘只在主窗 `Keys.onPressed` 内分发到 `ActionRegistry::handleKey`，应用失焦后不生效；无 `RegisterHotKey` / 全局快捷键注册。 | P1 | 中 |
| 系统媒体控制中心（SMTC / Now Playing / MPRIS） | 没有 | 全仓无 Windows SMTC、macOS Now Playing、Linux MPRIS 集成；任务栏/控制中心看不到曲目与播放按钮。 | P2 | 中 |
| 手机 / 网页遥控 | 不该做 | 需要常驻 HTTP/WebSocket 服务、设备配对与鉴权，并新开监听端口；当前是"本地播放器 + 媒体服务器客户端"架构，没有服务端与鉴权模型，加进来会扩大攻击面且与定位冲突。 | P2 | 大 |

## 9. 网络播放（URL / HTTP / RTSP / 直播 / 投屏）

| 条目 | 状态 | 判定依据（文件 / 行为） | 优先级 | 成本 |
|---|---|---|---|---|
| HTTP / HTTPS 直链 | 有但残缺 | 已有：`MpvClient::openFile` 对含 `://` 的地址原样交给 mpv，`--open`、`palune://`、媒体库条目均可进入。缺：界面没有"打开网络地址"输入框；`WindowController::openFileDialog` 只选本地文件；拖放 URL 仍按 `urls[0]` 处理，不走网络地址分支。 | P0 | 小 |
| RTSP / RTMP / HLS 等直播流 | 有但残缺 | 协议层由 mpv/FFmpeg 承担，代码未拦截 scheme；但无 UI 入口、无重连/超时/直播缓冲策略，也没有"直播中不可 seek"的界面处理；仓库内无 RTSP/HLS 实机证据。 | P1 | 中 |
| 媒体服务器串流播放 | 已有且可用 | Jellyfin/Plex 适配器返回可播放地址，交给同一个 `openFile`；播放进度回写媒体服务器。 | P1 | —（已实现） |
| 投屏（DLNA / Chromecast / AirPlay） | 没有 | 全仓无 SSDP/UPnP、Cast、AirPlay 相关实现；无"投屏到设备"UI。 | P2 | 大 |

---

## 10. P0 缺口（按建议顺序）

1. **播放列表 / 队列**（6.5）：目前只有"最近打开"，无法建队列、追加、排序、保存。
2. **播放列表拖放**（7.5）：多文件拖入只播第一个，且替换当前播放。
3. **章节**（2.6）：页签存在但写死"没有章节信息"，带章节的 MKV/蓝光转制文件完全不可用。
4. **在线字幕搜索 / 下载**（3.3）：完全缺失。
5. **字幕字体与样式**（3.6）：只有字号/颜色/描边/位置，缺字体族等常用项。
6. **画面比例**（1.5）：无 16:9 / 4:3 / 原始 / 填充切换。
7. **截图**（5.3）：无截图命令、快捷键、保存目录。
8. **打开网络地址**（9.1）：能力在，界面无入口。

## 11. P1 缺口条目名（18 条）

HDR 直通与峰值控制、旋转、音频直通、A-B 循环、循环播放入口、跳片头片尾、双字幕、ASS 样式覆盖、均衡器、声道映射、音量归一化、媒体库搜索/排序、窗口置顶、迷你模式、多实例、视频文件关联、全局媒体键、RTSP/直播流完善。

## 12. P2 缺口（6 条）与"不该做"（2 条）

P2：裁切、书签、色彩管理、锐化/降噪、系统媒体控制中心、投屏。

不该做：录像/片段导出（受 libmpv-only 内核约束）、手机/网页遥控（需常驻服务与鉴权，超出当前架构与定位）。

---

## 13. 说明

- 本轮未改任何代码；上表只回答"有没有、缺什么、值不值得做"。
- P0/P1/P2 是实现顺序建议，不是承诺；用户挑选后再按优先级拆任务。
- "已有且可用"的判定基于实际执行链路（动作 → 函数 → mpv 属性/命令）；"有但残缺"的条目已写明缺的具体属性或 UI 链路。
