# 1.0.0-RC：六条 UI 修复 + P0 五组证据

- 生成日期：2026-11-09（Asia/Shanghai）
- 构建：`build/Release/Palune.exe`（Release，Qt 6.8.3，MSVC 19.52 / VC Redist 14.52，DPR 200%）
- 安装包：`dist/Palune-1.0.0-win-x64.exe`
- 取证方式：真实进程 + 真实鼠标输入（`SetCursorPos` + `mouse_event`，逻辑坐标经 `ClientToScreen` 换算），不是截图模式摆拍
- 复验脚本：`qa/runtime/probe_rc_ui.py`、`qa/runtime/probe_theme_click.py`
- 原始数据：`docs/evidence/rc-ui-probe.json`、`docs/evidence/settings-probe-rc.json`、`docs/evidence/shots/rc-ui-fixes/`

## 一、六条 UI 修复

| # | 条目 | 根因（对象 / 层 / 机制） | 改法 | 验证证据 |
|---|---|---|---|---|
| 1 | 窗口按钮图标不统一 | `WindowButtons.qml` 用字符字形充当图标（`—` / `⍐` / `□` / `✕`）。字形宽度、基线、粗细由字体决定，三个按钮在多语言字体回退与 200% DPR 下大小/位置不一致；最大化后也没有还原态图形 | 改为纯矢量几何：固定 10×10 图标区、1px 描边，三按钮同尺寸同基线。最小化一条横线；最大化空心方框；最大化后换成两个错位方框（还原）；关闭两条 ±45° 斜线。颜色统一取 `tokens.text` | `buttons-A.png` / `buttons-B.png` / `buttons-C.png`（300×220 裁剪，三主题三按钮同形同粗细）；点击最大化后客户区 3840×2064（可用区），再点还原回 2360×1400，最小化后 `IsIconic=true`（`rc-ui-probe.json` 的 `maximize` / `restore` / `minimize`） |
| 2 | 标题栏与控制栏热区未分离 | 拖动热区 `dragStrip` 是铺在窗口顶部的 `MouseArea`，高度原先与控制栏口径混用；和底部控制栏悬停热区在矮窗口下叠在一起，按下事件被拖动层先吃掉，出现「想点控制条却把窗口拖走 / 想拖标题栏却触发控制栏」 | `dragStrip` 只占顶部且按主题定高（A 94 / B 52 / C 44），几何上与底部热区不相交；控制栏悬停改用 `HoverHandler`（只收悬停、不消费点击），隐藏态不会拦住视频点击。布局内控件（含窗口按钮）始终在热区之上 | 同一窗口同一次会话：在 y≈40 拖动 → 窗口 (738,330)→(978,410)，`dx=240, dy=80`；在 y≈645 控制栏拖动 → 位移 0（`rc-ui-probe.json` 的 `titleDrag` / `controlDrag`） |
| 3 | 布局 B/C 点了不生效 + 用真实名称 | 主题卡片 delegate 是 `Column`，子 `MouseArea` 用 `anchors.fill: parent`。positioner（Column）接管子项几何后 anchors 失效，命中区退化为 0×0，点击穿透到下层，`win.action("setTheme")` 从未触发。此前只在代码里看到连接存在，误判为已生效 | delegate 改成固定 108×88 的 `Item`，`Column` 用 `anchors.fill` 做内容，`MouseArea` 作为兄弟节点铺满整卡；补 `hoverEnabled`/`cursorShape`/`ToolTip`。名称改为「A · 经典」「B · 工作台」「C · 沉浸」，各自带一句定位说明 | 真实点击卡片：B → `themeAfter="B"` 且主窗换成工作台（`theme-click-B.json`、`theme-click-B-main.png`）；C → `themeAfter="C"`，设置窗同步缩到 1120×904、主窗换成 C 胶囊（`theme-click-C.json`、`theme-click-C-main.png`）；三主题设置页见 `settings-A-theme.png` / `settings-B-theme.png` / `settings-C-theme.png` |
| 4 | 滚动条遮挡内容 | 内容列宽取 `contentFlick.width`，悬浮式 `ScrollBar` 宽 8px 直接压在内容右缘上：最右侧控件被盖住、长文本贴边不可读 | `contentColumn.width = contentFlick.width - 12`，并显式声明 `ScrollBar.vertical.width = 8`。滚动条独占右侧 12px，内容不再被覆盖 | `settings-B-keys-scroll.png`（快捷键页 40 项，右侧留白与滚动条分离，改键/解绑按钮完整可点） |
| 5 | 「打开主页」指向不存在的站点 | `UpdateService` 里主页写死 `https://palune.app/`，该域名无服务（TLS 握手失败），点开是错误页 | 改为本仓公开地址 `https://github.com/yunyu697/palune`，并注明原因 | `homepage-link.json`：`https://github.com/yunyu697/palune` HEAD 200；`https://palune.app` 连接失败 |
| 6 | 设置窗口不是真正独立窗口 | `SettingsWindow` 用 `Qt::Tool` + `setTransientParent(owner)`，`WindowController` 还监听了主窗 `x/y/width/height` 四个变化去 `reposition`。结果：设置窗随主窗移动/缩放，主窗最小化时被一起收起，位置由主窗决定 | 改为 `Qt::Window \| FramelessWindowHint \| NoDropShadowWindowHint`，去掉瞬态父窗；`reposition()` 只在显示时按屏幕可用区居中一次；删除四条跟随连接与方法声明。保留原生 `WS_EX_TOOLWINDOW`，避免任务栏出现重复条目 | `rc-ui-probe.json` 的 `settingsIndependent`：`ownerHandle=0`；主窗被移动 (+160,+90) 后设置窗仍在 (900,372)，`samePosition=true`；`settings-independent.png`。另有 `libraryOverlayDrag` 证明覆盖层空白区拖动不再带动主窗（`moved=false`，`library-overlay-blocked.png`） |

## 二、P0 硬缺口

### 1. 存储整组

| 能力 | 配置键 | 默认值 | 生效时机 | 证据 |
|---|---|---|---|---|
| 缩略图缓存上限 | `[storage] thumbCacheLimitMb` | 2000 MB（UI 范围 100–20000，步进 100） | 下次扫描结束时按最旧优先淘汰；「按上限立即清理」可立刻执行 | 探针将上限设为 100 MB：115,343,360 B → 57,671,680 B，文件数 1 |
| 日志保留天数 | `[storage] logRetentionDays` | 14 天（UI 范围 1–365） | 修改后立即清理一次，之后每次启动再清理 | 探针造了超期日志：`oldLogExistsBefore=true` → `oldLogExistsAfter=false` |
| 占用展示 | `Preferences::storageUsage()` | — | 进入存储页即读，之后每 2.5 s 刷新 | `storageUsage`：配置 962 B、日志 4,857 B、索引 0 B、缩略图 0 B、合计 5.7 KB |
| 清理缩略图缓存 | — | — | 点击立即执行 | `library.clearThumbCache()`，随后 `refreshUsage()` |
| 重建媒体库索引 | — | — | 点击立即删除并重建 | `indexExistsBefore=true` → `indexExistsAfter=false`（重建期先删旧索引） |
| 配置导出 | — | — | 点击立即写文件（637 B） | `exported=true, exportBytes=637` |
| 配置导入（迁移） | 全量 `settings.toml` | — | **重启后生效**；导入前把旧配置备份为 `settings.toml.import-backup` | `imported=true`，`importRestoredAutoPlay=true`（导入后 `ui.autoPlay` 回到 true） |
| 打开配置目录 | — | — | 点击调用 `openStorageDir()` | 探针 `licenseActionInvoked` 同层验证系统打开调用 |

对应界面：`settings-B-storage.png`。原始数据：`settings-probe-rc.json` 的 `storage` / `storageUsage` 两段。

### 2. 界面语言控件

- 键：`[ui] language`，取值 `system`（默认，跟随系统）/ `zh_CN` / `en_US`；选项控件写入即落盘，**重启后生效**（翻译在 `QApplication` 启动时装载），界面提示已如实写明。
- 不是静态文案：设置页是真实的三选一控件（跟随系统 / 简体中文 / English），选中值来自 `prefs.language` 而非硬编码文本。
- 证据：`_scratch/cfg-lang-en/settings.toml`（`language = "en_US"`）冷启动 → `lang-en-B.png`（工作台布局全英文，含 "Storage" / "Media Library" / "Classic/Workbench/Immersive"）；`_scratch/cfg-lang-zh/settings.toml`（`language = "zh_CN"`）→ `lang-zh-B.png`（全中文）；`settings-en-US.png` 为英文下的设置页。
- 翻译完整性：`i18n/palune_en_US.ts` 新增 9 条 `SettingsPanel` + 3 条 `ThemeRegistry`（原先 C++ 回退提示用 `QStringLiteral`，切英文仍显示中文），重新 `lrelease` 后 `tools/ci/check_i18n.py` 通过。

### 3. 账户与媒体库入口

- 设置左侧导航新增「媒体库」页（第 6 项），不再要求用户回主界面找入口。
- 页面内容：账户状态说明（本地模式，无账户/登录/云同步，显式延后到 1.1）、条目数 / 根目录数 / 扫描状态、索引文件路径、添加目录、重新扫描、**打开媒体库**（`win.action("openLibrary")`）、打开索引目录、默认视图（`[library] viewMode = poster / list`，即时生效并持久化）、根目录列表与移除。
- 证据：`settings-B-library.png`；`settings-probe-rc.json` 的 `library` 相关读取与 `keyRows=40`。

### 4. 字幕描边预览真实联动

- 根因：播放器侧把设置值映射成 mpv 的 `sub-border-size = outline × 2`，预览侧却直接把 `outline` 当成描边宽度（1px），两侧口径不一致，实机 4px 预览 1px。
- 改法：预览统一用 `prefs.subtitleOutline * 2`，与 mpv 同口径；指标文字同步显示该值；`OutlinedText` 上限从 6 提到 12（对应 `outline` 上限 6）；提示文案写明「播放器实际描边 2px = outline × 2，预览同口径」。
- 证据：`sub.outline = 2.0` → mpv `sub-border-size = 4.000000`（`settings-probe-rc.json` 的 `mpv`）；同一配置截图 `subtitle-outline-4px-peak600.png` 显示「字号 24 · 描边 4.0px · 底部安全距 12%」。

### 5. 布局 B 第三栏真实数据

- 根因：第三栏原是占位文案「曲线预览不可用」，字号/指标/曲线/目标峰值都不参与渲染。
- 改法：`Canvas` 实算 PQ OETF/EOTF + ITU-R BT.2390 EETF（`KS = 1.5·maxLum − 0.5`，Hermite 段），输入 0–10000 nits，虚线为不压缩参考线；指标文字由同一组函数反算，不是写死字符串。字号、描边、底部安全距直接取 `prefs`。`targetPeakNits` 变化触发 `requestPaint()`。
- 证据：`[playback] targetPeakNits=600` + `[sub] fontSize=24, outline=2, position=12` 时，截图 `subtitle-outline-4px-peak600.png` 显示「字号 24 · 描边 4.0px · 底部安全距 12%」「拐点 ≈ 600 nits · 10000 nits 输入映射到 600 nits」，曲线拐点随目标峰值移动；默认 800 nits 时指标同步变为 800（见 `settings-B-subtitle.png`）。

## 三、未闭合项（不得对外称收口）

- 十项设置中「许可打开」只能证明「文件存在 + `openLicense` 分支被调用」，外部查看器是否成功弹窗无法自动断言 → 保持 PARTIAL。
- 代理只能证明「不可达端点下真实请求失败并回 UPD-001」，企业代理成功链路需真实代理服务，未验证。
- 未做 macOS/Linux 真机、MSI、生产签名。
