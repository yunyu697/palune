# alpha8 实机 UI 缺陷清理与设计稿核对

更新时间：2026-11-08（Asia/Shanghai）
版本：`1.0.0-alpha8`
对照设计：`temp_20261106_04/screenshots/{a,b,c}-*.png`、`prototype-*.html`、`docs/DESIGN.md`
实机截图：`docs/evidence/shots/alpha8-ui/`（21 个状态，真实窗口 `PrintWindow(2)` 合成）
几何记录：`docs/evidence/shots/alpha8-ui/geometry-alpha8.json`

## 0. 环境与验证方式（事实）

| 项 | 值 |
|---|---|
| 屏幕 | 3840×2160 @200%（DPR=2，逻辑 1920×1080） |
| 会话状态 | Windows 锁屏（前台窗口为「Windows 默认锁屏界面」） |
| 抓屏方式 | `PrintWindow(PW_RENDERFULLCONTENT=2)` 抓真实窗口 DWM 表面；`BitBlt`/`CopyFromScreen` 在锁屏下只能拿到壁纸，不可用 |
| 输入注入 | 锁屏下无法注入真实鼠标/键盘，拖动、贴边、实时 DPI 切换无法用真实指针完成；改用 `SetWindowPos` 驱动移动/缩放、`QT_SCALE_FACTOR` 驱动 DPI 各档重启，并在本文件中如实标注 |
| 辅助材料 | `qt-grab-*.png`（`QQuickWindow::grabWindow()`，应用内渲染）只作辅助，不作为判定依据 |

## 1. 四条现象：根因与修复对照

### 1.1 三条「方框」是同一个根因

| 现象 | 出现位置/形态 | 根因（对象 / 层 / 原因） |
|---|---|---|
| ① 打开后画面内部一个方框 | 窗口正中 318×318 物理（159 逻辑）空心矩形，随时间/视频内容无关，隐藏视频窗后仍在 | 覆盖窗 `OverlayWindow` 的 **系统非客户区（NC）合成层**。窗口创建后由 `applyNativeStyles()` 追加 `WS_THICKFRAME\|WS_MINIMIZEBOX\|WS_MAXIMIZEBOX`，Windows 把无边框窗口重新当成「有框窗口」，在 DWM 合成表面上绘制 NC 边框/框内残留矩形；不在 QML、不在视频窗像素 |
| ② 窗口边缘一圈很明显的方框 | 外缘 2–4 物理 px 浅色（#f3f3f3→#f4f7fc）圆角框，失焦/重绘后最明显 | 同上：`WS_THICKFRAME` 触发的 **非激活态 NC 边框绘制** |
| ③ 最大化/全屏时又出现一个方框 | 最大化 `3840×2064`、全屏 `3840×2160` 时同一圈边框变成直角、再次显形 | 同上：几何变化后系统重新走 NC 绘制；Region 在最大化时由 COMPLEX(3) 变 SIMPLE(2)，圆角框因此变成方正框 |

**判定证据（逐项可复现）**

1. `qa/runtime/probe_frame.py` / `probe_border.py`：同一窗口 `PrintWindow` 有框，`QQuickWindow::grabWindow()` 与 `QT_QUICK_BACKEND=software` 均无框；视频窗单独抓取无框。
2. `qa/runtime/probe_settings_focus.py`：设置窗（Qt::Tool，无 `WS_THICKFRAME`）无论激活/失焦边缘都是自身卡片色（38,38,38 / 12,13,16）；给它运行时追加 `WS_THICKFRAME|MIN|MAX` 后，边缘立刻变成 (243,243,243) —— 同一根因的可控复现。
3. `qa/runtime/probe_focus_border.py`：覆盖窗失焦后边缘像素 `(227,227,227),(255,255,255),(245,248,252)`（浅色框）；修复后同一时刻为 `(0,0,0),(26,26,26)`（自身深色底）。
4. `qa/runtime/probe_square.py`：中心方框在「隐藏视频窗」「移动视频窗」时都不变；修复后四组状态全部 `None`。

### 1.2 修复

在 [OverlayWindow.cpp](file:///D:/Users/Tyler%20Zhang/Documents/Trae-CN/temp_20261106_08/palune/src/window/OverlayWindow.cpp) 用 `SetWindowSubclass` 接管覆盖窗消息：

```cpp
if (msg == WM_NCCALCSIZE && wParam == TRUE) return 0;      // 客户区 = 整窗
if (msg == WM_NCACTIVATE || msg == WM_NCPAINT) return TRUE; // 吞掉系统 NC 绘制
```

- 保留 `WS_THICKFRAME`，因此 `startSystemMove/startSystemResize` 的系统缩放、Aero Snap、跨 DPI 行为不变。
- 追加一个 `SetWindowPos(..., SWP_FRAMECHANGED)` 让已创建的原生窗立即按新 NC 规则重算。
- 同时把 `Main.qml` 的窗口发丝描边从 `#33ffffff`（设计稿上限之外）降到 `#1affffff`（≈0.10），内缩 0.5，最大化/全屏不绘制，消除「自绘边框 + 系统边框」的第二层来源。

### 1.3 现象 ④：设置被遮挡并被限制在窗口内

| 项 | 修复前 | 修复后 |
|---|---|---|
| 承载对象 | `SettingsPanel.qml` 作为主窗内的 `Item` 弹层（受主窗裁剪、被主区压住） | 新增 [SettingsWindow](file:///D:/Users/Tyler%20Zhang/Documents/Trae-CN/temp_20261106_08/palune/src/window/SettingsWindow.h)：独立顶层 `QQuickView`，`Qt::Tool \| Qt::FramelessWindowHint`，`setTransientParent(owner)`，脱离主窗父子裁剪 |
| 尺寸/圆角 | 受父窗口尺寸约束 | 按主题：A 1020×660 / B 1080×660 / C 560×452；圆角 A/C 18、B 8，并同步 `CreateRoundRectRgn` 原生 Region |
| 位置 | — | A/B 以主窗居中；C 贴主窗右下 16px（修复了 `open()` 先 `reposition()` 后 `show()` 导致首次居中） |
| 关闭 | 隐藏弹层 | `hide()` + 主窗 `requestActivate()`；实测关闭后可见窗口只剩覆盖窗 + 视频窗，无残留 |
| 置顶关系 | — | `EnumWindows` 顺序（Z 序自上而下）中设置为第一项，位于覆盖窗/视频窗之上 |

## 2. 逐屏设计稿核对（真实窗口）

判定：PASS = 与设计稿结构/尺寸一致；DIFF = 仍存在已知差异；NOTE = 受锁屏/外部环境影响，仅记录。

| 屏幕 | 期望（设计稿） | 实机（alpha8） | 判定 |
|---|---|---|---|
| 主界面 A | 悬浮标题/品牌直接压在画面上，右上窗控，底部悬浮控制条 | 标题条改透明，品牌/文件名/窗控浮于视频；外缘无框；底部控制条留 10px 外边距 | PASS（标题条形态）/ DIFF（控制条仍为两行满宽，原型为单行浮动） |
| 主界面 B | 顶栏实底、右侧检查器为浮起卡片、底部技术条+控制条 | 检查器加右/上/下 12px 外边距与 8 圆角，视频右内缩 dock+12；技术条/控制条齐底 | PASS |
| 主界面 C | 精简直播：全幅画面 + 左上标题胶囊 + 右上窗控 + 底部胶囊控制条 | 一致；外缘干净 | PASS |
| 空态 A/B/C | 仅保留必要入口与右上窗控，不出现方框 | 一致；空态卡片居中无截断 | PASS |
| 媒体库 A/B/C | A 右侧滑出侧栏、B 居中宽面板、C 右侧抽屉 | 一致；面板不压底部控制条，关闭按钮可见 | PASS（内容源仍未接入，转 M4） |
| 设置 A/B/C | A 1020×660 三栏、B 1080×660 三栏、C 560×452 两栏；独立于主窗 | 三主题尺寸/圆角/三栏（A 含「字幕预览 + BT.2390」）一致；C 右下 16px，无裁剪 | PASS |
| 最大化 | 边框归零、贴工作区 3840×2064、无方框 | Region SIMPLE(2)，边缘像素为内容色，无浅色框 | PASS |
| 还原 | 回到 2360×1400；无残留旧几何 | 一致，覆盖窗/视频窗 offset 0 | PASS |
| 全屏进/出 | 3840×2160，chrome 隐藏；退出恢复原几何 | 一致，四边像素均为画面颜色 | PASS |
| 移动 | 拖出后视频窗同步 | `SetWindowPos` 后覆盖窗/视频窗几何完全相同（offset 0） | PASS（无真实指针，见 §0） |
| 缩放 | 1600×1000 后布局重排不截断、无框 | 控制条/标题/检查器按新宽度重排；边缘无浅色框 | PASS |
| DPI 100/150/200 | 逻辑尺寸一致、控件不糊、不截断 | 三档布局一致；`dpi-200` 的 `grabWindow` 为 2360×1400 物理（DPR=2） | PASS（用 `QT_SCALE_FACTOR` 重启模拟，非实时切换） |

## 3. 本轮未修（如实记录，不阻塞 alpha8）

1. **A 顶栏细节**：原型是左侧 macOS 交通灯 + 居中片名 + 右侧工具图标；当前是左侧 `PALUNE` 品牌 + 右侧 Windows 风格 −□× 窗控。功能完整，属于形态差异。
2. **A 底栏细节**：原型为单行浮动条（时间紧邻播放按钮）；当前为「进度行 + 按钮行」两行满宽条。
3. **B 检查器内容**：原型列出分辨率/码率/HDR/音轨/字幕等多行技术信息；当前为 6 行（文件名、路径、时长、音轨/字幕、编码、速度）。
4. **B 视频黑边**：顶栏为实底，视频窗仍按整窗高度居中，16:9 素材在顶栏下有约 36 逻辑 px 的 letterbox 黑带。
5. **媒体库内容源**：仍为空态（M4）。

## 4. 复跑命令

```powershell
# 真实窗口全量逐屏（21 状态）
python qa\runtime\alpha8_walkthrough.py
# 外缘/中心方框体检
python qa\runtime\alpha8_edge_audit.py
# 根因复现（NC 边框、失焦、方框归属）
python qa\runtime\probe_focus_border.py
python qa\runtime\probe_settings_focus.py
python qa\runtime\probe_square.py
```
