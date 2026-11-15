# Palune 界面规范（Design System）

本文件是 Palune QML 界面的唯一规范来源。所有间距、字号、行高、圆角、描边、控件高度、图标尺寸、动效时长、断点
都必须来自 `qml/tokens/Tokens.qml`，组件与布局里不允许写裸数值——由 `tools/ci/check_design_tokens.py` 强制。

- 令牌位置：`qml/tokens/Tokens.qml`（L0，零依赖单例）
- 分层：L0 令牌 → L1 组件（`qml/components`，不得 import 布局）→ L2 布局（`qml/layouts`，不得调播放器后端）
- 三套主题：A / B / C。**结构规则完全相同，差异只允许出现在令牌取值与布局模块选择上**；
  组件内不得出现 `themeId === "B" ? 12 : 8` 这类以主题切换尺寸/字号的写法（历史残留已清零，见 §11）。

参考基准：A/B 窗口 1180×700，C 1100×680（设计稿 §5.2）。

---

## 1. 间距标尺（4 基）

| 令牌 | 值 | 用途 |
| --- | --- | --- |
| `spaceHair` | 2 | 图标与文字之间的微间距、2px 轨道间隙 |
| `spaceXS` | 4 | 标签/徽标内边距、缩略图内元素间距 |
| `spaceS` | 6 | 紧凑行内间距、图标按钮内边距 |
| `spaceM` | 8 | 控件之间的默认间距、列表行左右内边距 |
| `spaceL` | 12 | 表单行之间、内容区内边距 |
| `spaceXL` | 16 | 区块之间的间距、卡片内边距 |
| `spaceXXL` | 20 | 面板外边距、大块之间 |
| `space3XL` | 24 | 页面级内边距、设置卡片外边距 |

写法：

```qml
ColumnLayout { spacing: tokens.spaceL }
Rectangle { anchors { left: parent.left; leftMargin: tokens.spaceM } }
Item { anchors.margins: tokens.space3XL }
```

禁止：`spacing: 10`、`anchors.leftMargin: 18`、`spacing: narrow ? 12 : 24`（三元分支同样是裸值）。
额外的一次性距离（如 110/112/368）一律提升为语义令牌（`toastInset`、`windowButtonsReserve`、`libraryDockReserve`）。

---

## 2. 字号阶梯

| 令牌 | 字号 | 行高令牌 | 字重 | 用途 |
| --- | --- | --- | --- | --- |
| `fontCaption` | 10 | `lineCaption`(14) | Normal / Medium | 标签、字段说明、徽标数字 |
| `fontSmall`（别名 `fontS`） | 11 | `lineCaption`(14) | Normal | 次要文本、时间码、页脚 |
| `fontBody`（别名 `fontM`） | 12 | `lineBody`(18) | Normal | 正文、按钮文字、表单值 |
| `fontBodyL` | 13 | `lineBody`(18) | Normal / DemiBold | 页签、强调正文、列表主标题 |
| `fontSubtitle` | 14 | `lineBody`(18) | DemiBold | 小标题、标题条（A 顶部） |
| `fontTitle` | 16 | `lineTitle`(22) | DemiBold | 面板标题、区块标题 |
| `fontDisplay`（别名 `fontL`） | 20 | `lineTitle`(22) | DemiBold | 页面主标题、空状态主文案 |

规则：

1. 阶梯外的字号一律不允许（9 / 15 已归一化为 10 / 14）。
2. 中英文混排：不设单独字号，统一用上表；中文行高按比例 1.4（`lineRatioBody`），标题 1.25（`lineRatioTitle`），
   多行文本用 `lineHeight: tokens.lineRatioBody; lineHeightMode: Text.ProportionalHeight`。
3. 等宽数字（时间码、码率、帧号）：`font.family: tokens.fontMono`（Consolas），字号固定 `fontM`，
   不随主题变化，避免播放时数字跳动导致进度条抖动。
4. 字距：`trackingS`(2) / `trackingM`(3) / `trackingL`(4)，用于全大写英文标签与空状态标题；
   中文字距保持 0。

---

## 3. 控件高度与命中区

| 令牌 | 值 | 用途 | 命中区 |
| --- | --- | --- | --- |
| `minHit` | 26 | 最小命中区（指针） | 26 |
| `hitPad` | 4 | 视觉小于 26 时向外扩的差值 | — |
| `ctrlHS` | 22 | 图标徽标、极小按钮 | 由父级扩到 26 |
| `ctrlH` | 26 | 紧凑按钮、页签、步进器单键 | 26 |
| `ctrlHL` | 30 | 标准按钮（PillButton 默认）、窗口按钮 | 30 |
| `ctrlHXL` | 32 | 次级动作按钮（更新页） | 32 |
| `ctrlHXXL` | 36 | 主行动按钮 | 36 |
| `rowH` | 30 | 设置行、列表行 | 30 |
| `rowHL` | 34 | 设置页签、列表行（含说明） | 34 |
| `rowHXL` | 56 | 双行列表行、媒体条目 | 56 |
| `switchW` / `switchH` / `switchKnob` | 38 / 20 / 14 | 开关 | 视觉 20，`anchors.margins: -tokens.hitPad` → 28 |
| `stepperW` / `stepperBtnW` | 112 / 26 | 步进器 | 26 |
| `trackHitH` | 18 | 滑杆轨道可视高度 | 由所在 `seekH` 容器承载 |
| `seekH` | 24 | 进度条组件高度 | 24（滚轮/拖动在整条上生效） |
| `knobSize` / `trackThickness` | 9 / 2 | 滑杆圆点与轨道 | — |
| `seekLabelW` / `volumeW` | 52 / 120 | 时间码与音量条宽度 | — |

判定标准（CI 的 D7）：`qml/components` 内控件的 `implicitHeight` 必须是令牌；令牌值 < `minHit` 时，
文件里必须出现 `anchors.margins: -tokens.hitPad` 作为命中区补偿，否则判红。

---

## 4. 图标

| 令牌 | 值 | 用途 |
| --- | --- | --- |
| `iconXS` | 6 | 状态点、指示符 |
| `iconS` | 10 | 窗口按钮线条、行内小图标 |
| `iconM` | 14 | 控件图标、列表图标 |
| `iconL` | 20 | 工具按钮、页签图标 |
| `iconXL` | 24 | 空状态、播放/暂停主按钮 |

对齐：图标与文字同行时垂直居中，图标外沿与文字基线区域的间距为 `spaceHair`/`spaceXS`；
线条图标描边宽度取 `stroke`，确保在 100%/125%/150% DPI 下都清晰。

---

## 5. 圆角、描边、阴影、分隔线、遮罩

| 令牌 | 值 | 用途 |
| --- | --- | --- |
| `radiusXS` | 4 | 徽标、缩略图内部元素、2px 轨道 |
| `radiusS` | 8 | 按钮、页签、列表行、输入框 |
| `radiusM` | 12 | 卡片、菜单、气泡 |
| `radiusL` | 18 | 设置卡片、对话框、播放错误卡 |
| `stroke` | 1 | 常规描边、分隔线宽度 |
| `strokeFocus` | 2 | 键盘焦点环 |

- 分隔线：`Rectangle { height: tokens.stroke; color: tokens.border }`；结构性分隔用 `tokens.border`，
  强调分隔用 `tokens.borderStrong`。
- 阴影/层次：不引入 DropShadow，用「半透明层 + 1px 亮边」模拟。
  `layerCard`(0xf2) 卡片底、`layerSolid`(0xff) 独立窗口底、`layerPopover`(0xf2) 菜单/提示条、
  `layerOverlay` 覆盖层底、`layerDanger` 错误卡底。
- 遮罩：模态/抽屉背后用 `tokens.scrim`（60% 黑），不用裸写 `#99000000`。
- 圆形元素：用 `radius: height / 2` / `radius: width / 2`，不写死 11/4.5 这类半高值。
- 窗口圆角只有一个来源：`ThemeRegistry::windowRadius`，取值必须落在上面的阶梯内
  （A 12=radiusM / B 8=radiusS / C 18=radiusL）。设置窗不再维护第二张表。
  QML 不读这个值去自绘窗口外框，只用于需要与原生裁切对齐的面。

### 5.1 窗口外框：交给系统，QML 不自绘

| 平台 | 圆角 | 边框 | 说明 |
| --- | --- | --- | --- |
| Windows 11 | `DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND` | `DWMWA_BORDER_COLOR` = 主题 `layerSolid` | 三个 DWM 属性必须一起下发：圆角偏好 + `DWMWA_USE_IMMERSIVE_DARK_MODE` + 边框色。只设圆角不声明暗色，系统会按浅色应用画一圈亮边 |
| Windows 10 | 无系统圆角 | 无 | 回退：`CreateRoundRectRgn` 原生裁切（硬边、无抗锯齿），不画描边 |
| macOS | 跟随系统窗口圆角 | 系统 | 不做 Region 裁切；无边框窗口没有系统圆角时保持直角，这是平台限制，不自绘补 |
| Linux | 无系统圆角 | 无 | 回退同上：不做自绘圆角描边，窗口直角 |

约束：

- QML 里**不允许**出现窗口级 `border` / `radius: win.windowRadius ± n`。窗口外框只有系统一层，
  应用内面板、按钮、输入框保留自己的圆角。
- 透明覆盖窗与视频窗的分工：视频窗负责画面与窗口形状（原生 Region），覆盖窗只负责内容，
  不画窗口底、不画窗口描边。两边都画就会出现「两圈边」。
- 静态检查 D8/D9/D10（`tools/ci/check_design_tokens.py`）拦住：窗口圆角越档、设置窗另起一张表、
  同一组件并存两档圆角、窗口圆角做偏移。

---

## 6. 动效

| 令牌 | 值 | 用途 |
| --- | --- | --- |
| `durationFast` | 120ms | 悬停/按下反馈、开关滑块 |
| `durationNormal` | 220ms | 面板开合、控制栏淡入淡出 |
| `durationSlow` | 320ms | 布局切换、媒体库抽屉 |
| `easingStandard` | `Easing.OutCubic` | 统一缓动；进入用 OutCubic，退出用 InCubic |
| `chromeHideDelay` | 2500ms | 控制栏自动隐藏延迟 |
| `toastDuration` | 2600ms | 提示条停留 |

规则：所有 `NumberAnimation` 的 `duration` 取上面三项之一；缓动显式写 `easing: tokens.easingStandard`，
不依赖默认值。控制栏显隐用「声明式 `running` 绑定 + 延迟令牌」，不用鼠标事件临时 restart。

---

## 7. 状态表达（A / B / C 三主题取值来源）

| 状态 | 令牌 | 值 | A | B | C |
| --- | --- | --- | --- | --- | --- |
| 静止（透明填充） | `stateRest` | `#14ffffff` | 玻璃条上 8% 白 | 扁平栏上 8% 白 | 胶囊上 8% 白 |
| 悬停 | `stateHover` | `#26ffffff` | 同上叠加 | 同上叠加 | 同上叠加 |
| 按下 | `statePressed` | `#33ffffff` | 同上叠加 | 同上叠加 | 同上叠加 |
| 列表行悬停 | `stateRow` | `#22ffffff` | — | 检查器行 | — |
| 斑马纹 | `stateZebra` | `#08ffffff` | 媒体库 | 媒体库 | 媒体库 |
| 表头/分组 | `stateHeader` | `#0affffff` | 列表表头 | 列表表头 | 列表表头 |
| 选中 | `accentSoft` / `accentFaint` | `#33e7b27c` / `#22e7b27c` | 页签底 | 页签底 | 页签底 |
| 焦点 | `strokeFocus` + `accent` | 2px | 键盘 Tab | 键盘 Tab | 键盘 Tab |
| 禁用 | `opacity: 0.45` | — | 全主题 | 全主题 | 全主题 |
| 危险 | `danger` / `dangerStrong` / `dangerHover` | — | 关闭按钮悬停、错误文案 | 同 | 同 |

**主题差异的边界**：三套主题只改底色与强调色（`bg`、`layerCard`、`accent` 等取值），
状态叠加值（`state*`）与结构规则一致：按钮在 A/B/C 下都是 30px 高、8px 圆角，只是底色不同。
若要给某主题换主色，改 `Tokens.qml` 的对应取值（或在主题层注入 palette），
**不要在组件里写 `themeId === "C" ? colorX : colorY`**。

---

## 8. 响应式断点

两套断点体系，职责不同：

| 类别 | 令牌 / 来源 | 值 | 改变什么 |
| --- | --- | --- | --- |
| 窗口紧凑（可配） | `ui.layout.<主题>.compactBelow`（`CompactController::kDefaultBreakpoint`） | 默认 560，允许 320–2400 | 低于阈值切紧凑：控制栏高度 `transportHeight/controlBarB/capsuleHeight` → `compactBarH`/`compactCapsuleH`，按钮文字隐藏只留图标，按钮由 `ctrlHL` 降为 `ctrlH` |
| 滞回 | `CompactController::kDefaultHysteresis` | 24 | 阈值上下各 24px 内不反复切换，防止拖拽边缘抖动 |
| 防抖 | `kDefaultDebounceMs` | 60ms | 连续 resize 合并判定 |
| 面板窄/矮 | `bpCompact` / `bpShort` | 720 / 520 | 设置面板：页签从左侧竖排改为顶部横排，内容区改 Flickable 可滚动，卡片圆角降为 `radiusS`，内边距降为 `spaceL` |
| 面板极矮 | `bpTiny` | 320 | 隐藏版本号副标题 |
| 面板极窄 | `bpNarrow` | 360 | 媒体库/检查器切单列，缩略图列数按 `Math.max(2, width / cell)` 计算 |
| 宽屏 | `bpWide` | 900 | 布局 A 的媒体库由抽屉改为右侧常驻栏（`libraryDockReserve` 让出空间） |

所有断点比较必须写 `tokens.bp*`，禁止 `width < 720` 这类裸写（CI 的 D6）。

---

## 9. 静态检查（接入 CI）

脚本：`tools/ci/check_design_tokens.py`（Python 3，无第三方依赖）

| 规则 | 内容 |
| --- | --- |
| D1 | 间距/内边距/外边距/字号/字距/行高/圆角/描边不得裸写数值，三元分支同样禁止；以 `*Radius` 命名的属性默认值也不得写数字（`0` 除外） |
| D2 | `qml/components` 内 `width/height/implicitWidth/implicitHeight` 不得裸写 |
| D3 | 交互状态色（`#14ffffff`/`#26ffffff`/`#99000000` 等）不得裸写 |
| D4 | 字号必须在 §2 阶梯内 |
| D5 | 用到的 `tokens.X` 必须在 `Tokens.qml` 中声明（防拼错导致运行期静默失效） |
| D6 | 断点不得裸写 |
| D7 | 组件高度令牌值 ≥ `minHit`，否则必须用 `hitPad` 扩命中区 |
| D8 | 窗口圆角只有一个来源：`ThemeRegistry::windowRadius` 的返回值必须在圆角阶梯内；`SettingsWindow` 不得另起一张表 |
| D9 | 同一组件文件内不得并存两档以上圆角（同一控件在不同状态跳档） |
| D10 | 窗口圆角不得做偏移（`win.windowRadius ± n`），窗口外框不自绘 |

实际输出（2026-11-15，本机 Release 构建）：

```
check_design_tokens: scanned 20 QML files, 143 tokens
check_design_tokens: OK
```

接入位置：`.github/workflows/ci.yml` 的 `layers` job（与 `check_layers.py`、`check_i18n.py` 同组），
`.github/workflows/release.yml` 出包前同一组检查。违规即退出码 1。

豁免写法：极少数需要按用户设置动态取值的属性（如字幕预览字号）用行尾注释
`// token-exempt: 字幕字号来自用户设置 prefs.subtitleSize`，并在评审中说明理由。

---

## 10. 变更流程

新增界面 / 组件：

1. 先在 §1–§8 找现成令牌；找不到再看是否应新增语义令牌（而不是就近凑一个数值）。
2. 新令牌写进 `Tokens.qml` 的对应分组，并在本文件表格补一行（名称/值/用途）。
3. 组件放 `qml/components`（L1），只允许 import `tokens` 与 QtQuick；不得 import `layouts`。
4. 交互控件必须满足 §3 高度与命中区；滑块/开关用 `hitPad` 扩命中区。
5. 在 `Main.qml`/布局里使用前，跑 `--qml-check`；提交前跑 `check_design_tokens.py`。
6. 新组件如果需要主题差异，只允许读令牌，不允许读 `themeId` 改尺寸。

改规范时需同步的位置（四处在同一次提交内改完，否则视为规范与实现分叉）：

| 改动 | 必须同步 |
| --- | --- |
| 增删标尺/字号/圆角令牌 | `Tokens.qml` + 本文档表格 + `check_design_tokens.py` 的阶梯集合（`FONT_TOKENS`/`RADIUS_TOKENS`） |
| 改命中区下限 | `Tokens.qml: minHit` + 本文档 §3 + 检查脚本 D7 说明 |
| 改断点 | `Tokens.qml: bp*` + 本文档 §8 + `CompactController` 默认值（若涉及窗口紧凑） |
| 改主题取值 | 只改 `Tokens.qml`（或主题注入层），本文档 §7 表格同步；不改组件 |

---

## 11. 本次归一化偏差清单

首轮全量扫描 20 个 QML（`Tokens.qml` 除外），共替换 **439 处**裸字面量
（尺寸/形状 340 处 + 颜色 101 处），其中 **61 处取值被归一化到标尺**，其余为同值改引用。

### 11.1 取值归一化（实际值 → 应为值）

| 属性 | 实际值 | 应为值 | 处数 | 说明 |
| --- | --- | --- | --- | --- |
| `spacing` | 10 | `spaceM`(8) | 16 | 控制栏按钮间距收紧 2px，整条更紧凑 |
| `spacing` | 18 | `spaceXL`(16) | 2 | 区块间距对齐 4 基 |
| `spacing` | 3 | `spaceXS`(4) | 1 | 图标与文字微间距 |
| 外边距 | 10 / 3 / 5 | `spaceM` / `spaceXS` | 10 | 缩略图内元素与行内边距 |
| 外边距 | 18 | `spaceXL`(16) | 3 | 媒体库与检查器内边距 |
| `font.pixelSize` | 9 | `fontCaption`(10) | 2 | 小于 10px 在高 DPI 下发虚 |
| `font.pixelSize` | 15 | `fontSubtitle`(14) | 2 | 阶梯外字号 |
| `radius` | 1 / 2 / 3 | `radiusXS`(4) | 7 | 微圆角统一 |
| `radius` | 6 / 7 / 9 | `radiusS`(8) | 8 | 按钮/行圆角统一 |
| `radius` | 10 | `radiusM`(12) | 2 | 菜单/气泡圆角统一 |
| `radius` | 16 | `radiusL`(18) | 2 | 卡片圆角统一 |
| `radius` | 11 / 4.5 / 3.5 / 2.5 | `height / 2`、`width / 2` | 5 | 胶囊/圆点改为几何推导，随尺寸自适应 |
| 断点 | 360 / 520 / 720 / 320 / 900 | `tokens.bp*` | 6 | 消除魔法数字 |

### 11.2 按文件

| 文件 | 替换处数 |
| --- | --- |
| `qml/SettingsPanel.qml` | 198 |
| `qml/UpdateSection.qml` | 52 |
| `qml/LibraryPanel.qml` | 49 |
| `qml/Main.qml` | 25 |
| `qml/layouts/LayoutB/LayoutB.qml` | 24 |
| `qml/components/WindowButtons.qml` | 19 |
| `qml/layouts/LayoutA/LayoutA.qml` | 17 |
| `qml/components/SeekBar.qml` | 11 |
| `qml/layouts/LayoutC/LayoutC.qml` | 8 |
| `qml/components/VolumeBar.qml` | 8 |
| `qml/components/SettingStepper.qml` | 7 |
| `qml/components/EmptyState.qml` | 5 |
| `qml/components/SettingCycle.qml` | 5 |
| `qml/components/SettingSwitch.qml` | 5 |
| `qml/components/SettingRow.qml` | 4 |
| `qml/SettingsWindow.qml` | 1 |
| `qml/library/MediaLibraryEmpty.qml` | 1 |

### 11.3 行为不变的证据

- `Palune.exe --qml-check`：21 个 QML 全部加载通过（`qml-check.json: ok=true, count=21`）。
- `tools/release/check_qml.ps1`（qmllint）：21 个文件，未发现结构性错误，退出码 0。
- `cmake --build build --config Release`：全量编译通过（本次只改 QML 与检查脚本，未改 C++ 逻辑）。
- 归一化只改数值标尺，不改控件结构、布局槽位与主题选择逻辑。

---

## 12. 按规范新写组件的示例

一个「按 §1–§8 写出来、且能通过 D1–D7」的组件（可直接放到 `qml/components/StatusChip.qml`）：

```qml
import QtQuick
import "../tokens"

// 状态徽标：图标 + 文本 + 可选计数，高度取 ctrlHS，命中区由父级行高保证。
Rectangle {
    id: chip

    property string text: ""
    property string icon: ""
    property bool active: false
    readonly property bool hovered: hover.hovered

    Tokens { id: tokens }

    implicitHeight: tokens.chipH          // 22，>= 唯一允许的 ctrlHS 档
    implicitWidth: row.implicitWidth + tokens.spaceM * 2
    radius: height / 2                    // 胶囊，几何推导
    color: chip.active ? tokens.accentSoft
                       : (chip.hovered ? tokens.stateHover : tokens.stateRest)
    border.width: chip.active ? tokens.strokeFocus : tokens.stroke
    border.color: chip.active ? tokens.accent : tokens.border

    Behavior on color {
        ColorAnimation { duration: tokens.durationFast; easing: tokens.easingStandard }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: tokens.spaceHair         // 图标与文字微间距

        Text {
            visible: chip.icon !== ""
            text: chip.icon
            color: tokens.text
            font.pixelSize: tokens.fontBody
            font.family: tokens.fontMono
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: chip.text
            color: chip.active ? tokens.accent : tokens.text
            font.pixelSize: tokens.fontSmall
            font.weight: chip.active ? Font.DemiBold : Font.Normal
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    HoverHandler { id: hover }
}
```

自查对照：间距全部 `space*`；字号 `fontBody`/`fontSmall` 在阶梯内；高度 `chipH`；圆角用几何推导；
描边 `stroke`/`strokeFocus`；状态色 `stateHover`/`stateRest`；动效 `durationFast` + `easingStandard`；
没有 `themeId` 分支，三套主题下结构一致。
