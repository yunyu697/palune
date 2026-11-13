# Qt 报错对话框：定位、处置与证据

日期：2026-11-13
范围：Windows x64，Qt 6.8.3 msvc2022_64，`1.0.0-RC` 代码线

## 1. 结论

用户看到的 Qt 报错框**只有一种来源**：Qt 平台插件初始化失败时由 Qt 自己弹出的原生英文框。它发生在 `QApplication` 构造之前，业务代码没有机会拦截：

```
This application failed to start because no Qt platform plugin could be initialized.
Reinstalling the application may fix this problem.
```

伴随 stderr：`qt.qpa.plugin: Could not find the Qt platform plugin "windows" in ""`。

**根因（本轮补齐，详见 `docs/evidence/packaging/plugin-root-cause.md`）**：发布包本身**不缺** `bin\platforms\qwindows.dll`（三份已发包装包内都在，哈希与 Qt 6.8.3 源文件一致）。部署版的 `Qt6Core.dll` 与 exe 同目录，Qt 的插件前缀由这个 DLL 的位置推导（= exe 目录），**只要运行目录里没有 `platforms\` 就必然 `qFatal`**。实际会踩到这条路的有两种：① 直接运行未部署的 `build\Release\Palune.exe`（本机 WER 2026-11-13 16:47 的 `c0000409` 报告即此，`LoadedModule` 无 qwindows.dll）；② 安装后被安全软件隔离/写入不完整。cwd、`QT_PLUGIN_PATH`、`QT_QPA_PLATFORM_PLUGIN_PATH` 均不足以单独造成失败（实测）。

排查同时确认：

| 候选来源 | 结论 |
| --- | --- |
| Qt 平台插件初始化失败（`no Qt platform plugin could be initialized`） | **存在**，本次复现并修复 |
| QML 运行时错误被转成对话框 | **不存在**。代码没有把 QML 错误接到任何对话框；QML 报错只走 `qWarning` |
| `qFatal` / 未捕获异常 / `std::terminate` 系统错误框 | 历史 WER 有 Palune 崩溃记录（`Qt6Core.dll` 异常码 `c0000409`、`ntdll.dll` `c0000374`），属于进程崩溃，不是这类“Qt 报错对话框”；本次未再复现 |
| 自己的错误提示被误当成 Qt 报错 | **不存在**。全仓只有两处 `QMessageBox`：退出确认（`WindowController.cpp:572`，默认关闭，`ui.confirmExit=false`）与自动化 selftest 关闭模态框（`AppController.cpp:1958`），都不是 Qt 告警 |

**没有**自定义 `messageHandler` 把 `qWarning`/`qCritical` 弹出来；改动前应用根本没有安装 `qInstallMessageHandler`。

## 2. 复现方式（不占前台）

缺 `qwindows.dll` 的部署副本 + `qa/runtime/probe_qt_dialogs.py`：枚举 `#32770`，`ShowWindow(SW_SHOWNOACTIVATE)` + 移到屏幕外，`PrintWindow` 离屏回读，不抢焦点、不播音频。

```powershell
python qa\runtime\probe_qt_dialogs.py --exe %TEMP%\palune-before\Palune.exe --bare --tag before
```

结果（`dialog-probe-before.json`）：标题 `Palune`，正文即上面的英文原文，截图 `dialog-before-1.png`。

## 3. 处置（按发布版标准）

分发层（让“插件不在”这件事不再发生，见 `docs/evidence/packaging/plugin-root-cause.md`）：

1. `CMakeLists.txt` POST_BUILD `windeployqt`：构建树自带 `platforms\`，直接运行 `build\Release\Palune.exe` 不再依赖外部 Qt 前缀。
2. `bin\qt.conf` 随包安装（`Prefix=. Plugins=. Qml2Imports=qml`），插件/QML 解析钉死在安装目录。
3. 打包门禁：`windeployqt` 缺失时 `FATAL_ERROR`；安装暂存与 `package_windows.ps1` 双重断言关键组件。
4. 安装器结束前落盘自检（缺文件中文提示 + `install-check.log` + 退出码 3）。

应用层：

1. **启动前自检**（`src/app/main.cpp`）：`QApplication` 之前**只认应用目录**的 `platforms/qwindows.dll`（不再把 `QLibraryInfo` 前缀算作可用，否则会在装了 Qt 的开发机上误判通过）；部署版自带插件时把 `QT_PLUGIN_PATH` 固定到自身目录并清掉 `QT_QPA_PLATFORM_PLUGIN_PATH`。
   - 找不到时不进入 Qt 初始化，直接写一条 `[ERROR]` 日志 + 一句中文可执行提示（重装 / 安全软件信任安装目录 / 发日志），不再暴露插件路径与 Qt 版本。
2. **Qt 消息统一进日志**（`src/diag/Log.cpp`，`main.cpp`）：`Log::installQtHandler()` 用 `qInstallMessageHandler` 把 Qt 的 debug/info/warning/critical/fatal 全部写入 `palune.log`；warning 以上附加来源 `文件:行号`；`QtFatalMsg` 先落日志再 `abort`。**不弹窗**。

## 4. 修复后证据

同一用例（`--bare --tag after`）：

- 弹窗正文变为：`Palune 缺少运行所需的组件，无法启动。\n请重新安装 Palune；如果仍然无法启动，请把日志文件发给支持人员。`
- `dialog-probe-after.json` 的 `stderr` 为空；`dialog-after-1.png`。
- `%PALUNE_CONFIG_DIR%\logs\palune.log` 记录（完整文件见 `palune-log-preflight.txt`）：

```
2026-11-13T17:14:37.599 [ERROR] 启动自检失败：找不到平台插件 qwindows.dll；应用目录：C:/Users/TYLERZ~1/AppData/Local/Temp/palune-preflight；Qt 插件前缀：C:/Users/TYLERZ~1/AppData/Local/Temp/palune-preflight/plugins
```

Qt 内部告警进日志的直接证据（`QT_QPA_PLATFORM=offscreen` 触发的无害字体告警；全程无窗口、无对话框）：

```
2026-11-13T17:14:57.447 [WARN] QFontDatabase: Cannot find font directory C:/Qt/6.8.3/msvc2022_64/lib/fonts.
Note that Qt no longer ships fonts. Deploy some (from https://dejavu-fonts.github.io/ for example) or switch to fontconfig.
```

该次运行 `dialog-probe-offscreen.json` 的 `dialogs` 与 `topLevelWindows` 均为空，证明告警没有变成任何窗口。

本轮又在**真实安装树**（带 `qt.conf`，仅把 `bin\platforms` 改名）上复验了一次：

- `dialog-probe-install-missing-plugin.json`：标题 `Palune`，正文为上表的中文提示，`stderr` 为空（Qt 根本没被初始化）。
- 同时刻日志 `%PALUNE_CONFIG_DIR%\logs\palune.log`：

```
2026-11-13T17:33:11.567 [ERROR] 启动自检失败：找不到平台插件 qwindows.dll；应用目录：…\palune-install-noplugins\bin；Qt 插件前缀：…\palune-install-noplugins\bin\plugins
```

注意“Qt 插件前缀”现在是应用目录下的 `plugins`，证明部署版 Qt 已不再回落到开发机的 `C:\Qt\…`。

## 5. 原本会弹的那个错误现在去哪了

| 触发 | 修复前 | 修复后 |
| --- | --- | --- |
| 平台插件缺失 | Qt 英文原生框 + `qFatal` 崩溃（WER `c0000409`） | 中文提示一句（可执行动作）+ `palune.log` 的 `[ERROR]` 明细；进程正常返回 1，不再崩溃 |
| Qt/QML 内部告警 | 默认 handler 打到 stderr；GUI 子系统下用户看不到，等于静默丢失 | 写入 `palune.log`，warning 以上带来源文件与行号 |
| Qt fatal | 系统错误框 / WER 崩溃 | 先写 `[ERROR]` 日志再退出，便于事后定位 |

## 6. 仍未覆盖

- 安全软件把 `platforms\qwindows.dll` 隔离掉这件事无法在本机安全制造；已由安装器落盘自检 + `docs/manual-acceptance.md` §5 覆盖。
- WER 历史记录里的 `ntdll.dll c0000374`（堆损坏）本次未复现，不能归因于本弹窗；若再次出现按崩溃单独处理。
- 外部查看器类弹窗无法在不打扰用户的前提下断言，已转入 `docs/manual-acceptance.md`。
