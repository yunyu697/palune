# 圆角白边与圆角不一致：定位、修复与验收证据

日期：2026-11-15
范围：Palune 主窗（A/B/C 三主题）、设置窗（独立顶层）、库面板/设置面板/关于页/播放控制条等应用内表面
构建：Windows，Qt 6.8.3，`cmake --build build --config Release`

## 0. 结论与状态口径

| 项 | 结论 | 证据等级 |
| --- | --- | --- |
| 深色界面四边/四角那圈浅白边 | 主因是 QML 自绘的窗口级发丝描边（1px `#1affffff` + `radius: windowRadius - 0.5`） | **已用像素剖面前后对照证明**（离屏渲染，见 §4） |
| 同一主题下主窗与设置窗圆角不一致 | 两张独立的窗口圆角表：`ThemeRegistry` 14/10/20，`SettingsWindow` B?8:18 | 代码证据 + 静态检查 D8 拦截（§5） |
| 系统在窗口外圈另画一道亮边 | 只下发 `DWMWA_WINDOW_CORNER_PREFERENCE`，没有声明暗色模式与边框色 | **未验证**（需真机合成，见 §6） |
| 真机窗口四角像素剖面（含 DWM/Region/透明合成） | 未采集 | **未验证**（§6） |

口径说明：本文只把 §4 的离屏 A/B 对照称为「已证明」；凡涉及 DWM、透明合成、双顶层窗叠加的行为，
一律写「未验证」，不写「已修复」。旧代码没有真机 A/B 基线，因此不使用「回归」措辞。

## 1. 逐层排除

候选层按「是否能在离屏/代码层被证伪」排序，逐层给出方法、结果与证据。

| 层 | 检查方法 | 结果 | 证据 |
| --- | --- | --- | --- |
| QML 窗口级 `border` / `radius` | 全量扫描 20 个 QML 的 `radius:`、`border.*`；对照修复前后离屏渲染的四角像素 | **命中**：`Main.qml` 有一条覆盖整窗的发丝描边 | §3.1、§4 |
| DWM 系统边框 | 读 `WindowController` / `SettingsWindow` 的 `DwmSetWindowAttribute` 调用点 | **命中（贡献项）**：主窗只设圆角偏好，未声明 `DWMWA_USE_IMMERSIVE_DARK_MODE` 与 `DWMWA_BORDER_COLOR`，Win11 会按浅色应用补一圈亮边；设置窗在 `showEvent` 里单独设过一次圆角 | §3.3 |
| 透明窗抗锯齿 | 检查 `WA_TranslucentBackground` + `SetWindowRgn(CreateRoundRectRgn)` 路径 | 原生 Region 是 1-bit 裁切、无抗锯齿，**它本身不产生白边**；但「Region 边缘」与「QML 描边」不重合会放大观感。真机像素未采集 | §6 |
| `MultiEffect` / `DropShadow` / 阴影 | 全量搜索 `MultiEffect|DropShadow|RectangularShadow` | **排除**：`qml/` 内无实际使用（仅 `Tokens.qml` 注释提到「不引 DropShadow」） | `qml/` 搜索 |
| 双顶层窗（覆盖窗 + 视频窗）叠加 | 读 `WindowController::applyWindowRegion`，两窗使用同一 `radius` 与同一 Region；视频窗无 QML 边框 | **排除为白边来源**；两窗边缘是否逐像素对齐仍需真机 | §3.1、§6 |
| QML 卡片的自身描边（设置/库面板） | 读 `SettingsPanel` / `LibraryPanel` 根卡片 | 设置窗独立模式 `border.width: 0`；主窗内弹层保留卡片描边（属应用内卡片，不是窗口外框） | `qml/SettingsPanel.qml:82` |

## 2. 复现方式（离屏，不上屏）

由于用户未授权真机开窗，且硬约束禁止把验证落在交互桌面，像素证据全部来自**离屏渲染**：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
$env:QT_QUICK_BACKEND='software'
Palune.exe --ui-shot <dir> --shot-matrix
```

- 输出 3 主题 × 13 界面 = 39 张 PNG，本机 DPR=2，尺寸 2880×1800（逻辑 1440×900）。
- `--shot-matrix` 用 `QQuickView + Main.qml` 的应用内渲染路径，`grabWindow()` 取帧；
  不创建桌面窗口、不抢焦点、不弹窗。
- 该路径**不含 DWM、不含桌面合成器、不含原生 Region**，因此只能证明 QML 层的绘制内容。

「修复前」对照的重建方法：

1. 备份 `qml/Main.qml`，记录 `git diff` 的 SHA-256。
2. 把被删除的那段逐字放回（含旧的每主题半径 14/10/20）：

```qml
Rectangle {
    anchors.fill: parent
    anchors.margins: 0.5
    visible: !win.fullscreen && !win.maximized
    radius: (root.themeId === "B" ? 10 : root.themeId === "C" ? 20 : 14) - 0.5
    color: "transparent"
    border.width: 1
    border.color: "#1affffff"
}
```

3. 重新构建 → 离屏截图 → 恢复 `Main.qml` → 重新构建。
4. 恢复后 `git diff` 与备份哈希一致（`7F38C0…A03C9`），确认工作树无残留临时改动。

## 3. 根因与修改

### 3.1 QML 自绘窗口描边（白边的直接来源）

原代码（`qml/Main.qml`，已删除）：

```qml
// 窗口发丝描边 ... 内缩 0.5px 避免被原生 Region 裁掉半边
Rectangle {
    anchors.fill: parent
    anchors.margins: 0.5
    visible: !win.fullscreen && !win.maximized
    radius: win.windowRadius - 0.5
    color: "transparent"
    border.width: 1
    border.color: "#1affffff"
}
```

- 颜色 `#1affffff` = 10% 白，叠在 `#0b0c10` 底上理论亮度 ≈ 13 + 0.102×(255−13) = **37.7**；
  实测描边像素 37（A/C）、41（B，底色更亮）。数值吻合，确认白边就是它。
- `radius: windowRadius - 0.5` 让描边半径比原生 Region 小 0.5px，两者边界不重合，深色下更明显。
- 修改：删除该 Rectangle；窗口外框交给系统（`qml/Main.qml:218-221` 保留说明注释）。
  主题切换时改为下发系统边框色：`qml/Main.qml:35-36` → `win.setWindowFrameColor(tokens.layerSolid)`。

### 3.2 两张不一致的窗口圆角表

| 位置 | 修复前 | 修复后 |
| --- | --- | --- |
| `ThemeRegistry::windowRadius` | A=14 / B=10 / C=20（不在 QML 阶梯 4/8/12/18 内） | A=12(radiusM) / B=8(radiusS) / C=18(radiusL) |
| `SettingsWindow::radiusForTheme` | `theme == "B" ? 8 : 18`（自己一张表） | `return ThemeRegistry::windowRadius(theme);` |
| `SettingsWindow.qml` 回退值 | `property int panelRadius: ... : 18`（裸值） | `... : tokens.radiusL`（新增 `import "tokens"`） |

同一主题下主窗与设置窗的圆角从此只有一个来源，且都落在令牌阶梯内。

### 3.3 系统窗框三个属性必须一起下发

修复前主窗只在 `SettingsWindow::showEvent` 里设过圆角偏好，主窗路径没有暗色声明。
新增 `src/window/SystemChrome.h`，把三件事合成一个入口：

```cpp
DwmSetWindowAttribute(hwnd, 33, &corner, ...);   // DWMWA_WINDOW_CORNER_PREFERENCE
DwmSetWindowAttribute(hwnd, 20, &dark, ...);     // DWMWA_USE_IMMERSIVE_DARK_MODE
DwmSetWindowAttribute(hwnd, 34, &color, ...);    // DWMWA_BORDER_COLOR
```

- 主窗：`WindowController::applyWindowRegion()` 在 `SetWindowRgn` 之后调用（`src/window/WindowController.cpp:791-817`）。
- 设置窗：`SettingsWindow::applyNativeRegion()` 调用（`src/window/SettingsWindow.cpp:171-186`）。
- 边框色来源：主题 `tokens.layerSolid`，主窗由 QML 通过 `setWindowFrameColor()` 下发；
  设置窗使用同一取值 `#0d0e11`。
- 全屏/最大化时 `logical = 0` → `DWMWCP_DONOTROUND`，避免系统在无边框全屏上补圆角。

## 4. 验收证据：四角像素剖面前后对照

数据文件：`qa/evidence/radius/corner-profiles.json`
截图：`qa/evidence/radius/{before,after}-offscreen/m2-{A,B,C}-main.png`
8× 角部放大：`qa/evidence/radius/{before,after}-offscreen/corner-{A,B,C}-8x.png`

### 4.1 左上角 3px 边缘带（x∈[0,200] 且 y<3，加上 y∈[0,200] 且 x<3；共 1206 像素）

| 主题 | 底色亮度 | 修复前：亮像素(≥底色+8) / 均值 / 峰值 | 修复后：亮像素 / 均值 / 峰值 |
| --- | --- | --- | --- |
| A | 13 | 726 / 1206，均值 27.4，峰值 37 | **0 / 1206，均值 13.0，峰值 13** |
| B | 17 | 752 / 1206，均值 31.0，峰值 41 | **0 / 1206，均值 16.0，峰值 17** |
| C | 13 | 684 / 1206，均值 26.5，峰值 37 | **0 / 1206，均值 13.0，峰值 13** |

上边缘中点（`x = w/2`, `y = 0..3`）：

| 主题 | 修复前 | 修复后 |
| --- | --- | --- |
| A | 13 / 37 / 37 / 13 | 13 / 13 / 13 / 13 |
| B | 17 / 41 / 41 / 17 | 17 / 17 / 17 / 17 |
| C | 13 / 37 / 37 / 13 | 13 / 13 / 13 / 13 |

左边缘中点同值（描边四边等宽）。

### 4.2 左上角对角线（`(0,0)` 起，步长 1 物理像素，取前 20 个）

| 主题 | 修复前 | 修复后 |
| --- | --- | --- |
| A | 13×8, **20, 37**, 13×10 | 13×20（全平） |
| B | 17×6, **38, 37**, 17×12 | 17×20（全平） |
| C | 13×12, **36, 28**, 13×6 | 13×20（全平） |

圆弧起点与旧半径严格对应（DPR=2）：

| 主题 | 旧半径（逻辑） | 物理半径 r | 对角线交点预测 `r(1−1/√2)` | 实测首个亮像素 |
| --- | --- | --- | --- | --- |
| A | 13.5 | 27 | 7.9 | (8,8) |
| B | 9.5 | 19 | 5.6 | (6,6) |
| C | 19.5 | 39 | 11.4 | (12,12) |

预测值与实测一致，可确认亮弧就是那条 `radius: windowRadius - 0.5` 的描边，而不是内容或阴影。

修复后对角线在 42 像素之后出现的亮值（B 的 45、C 的 48）是布局内容（B 顶部条、C 标题胶囊）进入采样区，
不在边缘带上；边缘带统计已把它们排除（0 个亮像素）。

## 5. 各界面圆角取值清点

扫描范围：`qml/**/*.qml`（不含 `Tokens.qml`），共 **61 处 `radius:`**。

| 文件 | 处数 | 取值 | 用途 |
| --- | --- | --- | --- |
| `qml/Main.qml` | 5 | radiusM / radiusL / radiusS | 右键菜单、提示条、错误卡、URL 对话框、输入框 |
| `qml/LibraryPanel.qml` | 8 | radiusL（抽屉 0）/ radiusS / radiusM / radiusXS / height/2 | 面板、缩略图、搜索框、徽标、圆点 |
| `qml/SettingsPanel.qml` | 19 | windowMode→`panelRadius`；narrow→radiusS；其余 radiusL/radiusS/radiusXS/height/2、width/2 | 卡片、页签、按钮、开关、步进器 |
| `qml/UpdateSection.qml` | 6 | radiusM / radiusS / radiusXS | 更新卡、镜像行、图标底 |
| `qml/components/*` | 15 | radiusL / radiusS / height/2 / width/2 | EmptyState、PillButton、SeekBar、SettingCycle/Stepper/Switch、VolumeBar、WindowButtons |
| `qml/layouts/*` | 8 | LayoutA radiusL×2；LayoutB radiusS×4；LayoutC radiusL + height/2 | 玻璃条、卡片、胶囊 |

结论：

- 61 处全部是 `tokens.radius{XS,S,M,L}` 或几何推导（`height/2`、`width/2`），**0 处裸数值**。
- 唯一的条件取值是 `LibraryPanel.qml:69` 的抽屉模式 `0`（直角）和 `SettingsPanel.qml:77` 的
  窗口模式/窄屏分支，二者都回落到令牌，不构成第二套阶梯。
- 窗口圆角取值：A=12（radiusM）、B=8（radiusS）、C=18（radiusL）；主窗与设置窗同源。
- `qml/Main.qml` 已无任何窗口级 `radius` / `border`（`windowRadius` 在 QML 中零引用）。

## 6. 未验证清单（不计入通过）

以下项目在离屏渲染与静态检查中无法覆盖，均**未验证**，需真机授权后单独复测：

1. Win11 真机窗口四角/四边像素剖面（DWM 圆角 + `SetWindowRgn` 裁切 + 桌面合成共同作用）。
2. `DWMWA_USE_IMMERSIVE_DARK_MODE` / `DWMWA_BORDER_COLOR` 是否真正消除了系统亮边。
3. 透明窗边缘的抗锯齿观感（原生 Region 为 1-bit 硬边）。
4. 覆盖窗与视频窗两个顶层窗的边缘是否逐像素对齐、切换时是否露出直角。
5. 设置窗作为独立顶层窗口的四角（本次只覆盖了主窗内的设置面板）。
6. macOS 系统窗口圆角（本机无法验证）。
7. Windows 10 无系统圆角时的 Region 回退。
8. 最大化/全屏往返后的角部。
9. 125% / 150% DPI 下的角部（本次只有 DPR=2 的离屏帧）。

方法边界：离屏 `QQuickView` 帧能证明「QML 画了什么」，不能证明「系统在窗口外圈画了什么」。
隐藏桌面上静态帧在交换链重建后近全黑的问题已在 `qa/evidence/layering/FINDINGS.md` 记录，本次不重复尝试。

## 7. 拦回归：静态检查与 CI

`tools/ci/check_design_tokens.py` 新增/扩展：

| 规则 | 内容 | 负向用例 |
| --- | --- | --- |
| D1（扩展） | 任何以 `*Radius` 命名的属性不得写数字默认值（`0` 除外） | `property int panelRadius: 18` → FAIL |
| D8 | `ThemeRegistry::windowRadius` 的返回值必须在令牌阶梯 4/8/12/18 内；`SettingsWindow` 必须引用它，不得另起一张表 | 返回 14/10/20 → FAIL |
| D9 | 同一 `qml/components` 文件内不得并存两档以上圆角 | radiusS + radiusM → FAIL |
| D10 | 窗口圆角不得做偏移（`windowRadius ± n`） | `win.windowRadius - 0.5` → FAIL |

负向用例（临时文件，验证后删除）实际输出：

```
check_design_tokens: scanned 21 QML files, 143 tokens
FAIL qml\components\_TmpRadiusCheck.qml:5 D1 圆角属性裸写 18（应为 tokens.radius*）
FAIL qml\components\_TmpRadiusCheck.qml:8 D10 窗口圆角不得做偏移（窗口外框已交给系统，QML 不再自绘窗口级描边）
FAIL qml\components\_TmpRadiusCheck.qml:1 D9 同一组件内并存 ['radiusM', 'radiusS'] 多档圆角
check_design_tokens: 3 violation(s)
```

正向结果（当前工作树）：

```
check_design_tokens: scanned 20 QML files, 143 tokens
check_design_tokens: OK
```

接入位置：

- `.github/workflows/ci.yml` 的 `layers` job：`设计令牌检查` 步骤（第 41–42 行）。
- `.github/workflows/release.yml` 出包前同一组检查（第 101 行）。

其他门禁：`pwsh tools/release/check_qml.ps1` → 21 个文件、无结构性错误；
`ctest --test-dir build -C Release` → 12/12 通过（含 qml_check）。

## 8. 变更文件

| 文件 | 变更 |
| --- | --- |
| `qml/Main.qml` | 删除窗口级发丝描边；主题变化时下发系统边框色 |
| `qml/SettingsPanel.qml` | 独立窗口模式卡片不自绘描边（避免系统边框内再叠一条线） |
| `qml/SettingsWindow.qml` | 圆角回退值改为 `tokens.radiusL` |
| `qml/tokens/Tokens.qml` | 删除无引用的 `hairline` |
| `src/theme/ThemeRegistry.cpp` | 窗口圆角 14/10/20 → 12/8/18（阶梯内） |
| `src/window/SettingsWindow.cpp` | 删除第二张圆角表与 `showEvent` 里的单独 DWM 调用；改走系统窗框入口 |
| `src/window/SystemChrome.h`（新增） | 圆角偏好 + 暗色模式 + 边框色一次下发 |
| `src/window/WindowController.{h,cpp}` | 新增 `setWindowFrameColor`；Region 之后统一下发系统窗框 |
| `tools/ci/check_design_tokens.py` | D1 扩展 + D8/D9/D10 |
| `docs/DESIGN-SYSTEM.md` | §5 / §5.1 / §9 同步 |
