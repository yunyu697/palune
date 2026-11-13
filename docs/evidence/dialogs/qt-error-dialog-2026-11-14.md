# Qt 报错弹窗复发排查（2026-11-14）

## 1. 现场核对结果：未取到用户那一刻的弹窗

检查时（2026-11-14 05:1x，本机）：

- `Get-Process Palune` / `Win32_Process`：**没有任何 Palune 进程**
- 可见顶层窗口里没有 `#32770`、标题含 `Palune`/`Qt` 的窗口
- 今日 `Application` 日志无 Palune 相关错误事件（最近两条是 2026-11-13 16:47 `c0000409`、15:51 `c0000374`，均为崩溃，不是本弹窗）
- 未安装任何副本：`%LOCALAPPDATA%\Programs\Palune`、`Program Files\Palune` 均不存在，注册表 Uninstall 无 Palune 项，桌面/开始菜单无快捷方式

结论：**用户看到的那一次弹窗已经不存在于系统里，无法直接枚举其标题、正文、pid 与模块列表**。以下改为逐条复现能产出 Qt 原生英文框的启动路径，并对命中的路径修复后复验。

## 2. 复现：唯一还能产出英文框的路径是外部环境变量

用现有构建树（`build\Release`，已带 `platforms\qwindows.dll` 与 Qt 运行时），在不加载任何外部 Qt 的会话里：

```powershell
$env:QT_QPA_PLATFORM = "bogus"
python qa\runtime\probe_qt_dialogs.py --exe build\Release\Palune.exe --bare --tag qpa-bogus
```

结果（`dialog-probe-1114-qpa-bogus.json`）：

- 弹窗标题 `Palune`，正文 **`This application failed to start because no Qt platform plugin could be initialized. Reinstalling the application may fix this problem.\n\nAvailable platform plugins are: windows.`**
- `stderr`：`qt.qpa.plugin: Could not find the Qt platform plugin "bogus" in ""`
- 截图：`dialog-1114-qpa-bogus-1.png`

这是**插件存在、但环境指定了不存在的平台名**：`QT_QPA_PLATFORM` 从开发终端/CI 继承下来（`offscreen`、`bogus` 等），Qt 在 `QApplication` 构造阶段直接 `qFatal`，上一轮的预检只查「文件在不在」，所以拦不住。

## 3. 修复（不靠“提示更友好”）

`src/app/main.cpp`：

1. `preparePlatformPlugins`：`SetDllDirectoryW(应用目录)`，插件依赖只从应用目录解析，避免 PATH 上的另一套 Qt 抢占。
2. 校验 `QT_QPA_PLATFORM`：请求的平台插件（`platforms/q<name>.dll`）不在应用目录时清掉该变量，回到默认 `windows` 平台。
3. 新增 `platformPluginLoadable`：在 `QApplication` 之前真正 `LoadLibraryExW(plugin, LOAD_LIBRARY_SEARCH_APPLICATION_DIR|SYSTEM32)`。失败则写 `[ERROR]` 日志（含 Win32 错误码）并给中文提示，不再落到 Qt 的英文框。

`CMakeLists.txt`：

4. 构建树 POST_BUILD 复制 `packaging/qt.conf`，与安装树一致（`Prefix=.`、`Plugins=.`、`Qml2Imports=qml`）。
5. NSIS 落盘自检新增 `bin\qt.conf`、`bin\vcruntime140.dll` 两项。

## 4. 修复后复验矩阵

| 场景 | 命令要点 | 结果 | 证据 |
| --- | --- | --- | --- |
| 构建树，正常 | `--bare` | 无对话框，主窗 + 视频窗 | `dialog-probe-1114-fixed-normal.json` |
| 构建树，`QT_QPA_PLATFORM=bogus` | 同上 + 环境变量 | **无对话框**，正常出窗 | `dialog-probe-1114-fixed-qpa-bogus.json` |
| 安装树（快捷方式等价：cwd=bin） | `--bare` | 无对话框 | `dialog-probe-1114-install-shortcut.json` |
| 安装树，cwd=`C:\Windows\System32` | 隐藏桌面 `--m2-config-check` | 退出码 **0** | `dist\evidence-1114\cwd-system32.json` |
| 安装树，System32 + `QT_QPA_PLATFORM=bogus` | 同上 | 退出码 **0** | `dist\evidence-1114\cwd-system32-qpa-bogus.json` |
| 插件文件缺失（改名 `platforms`） | `--bare` | 中文提示，无英文框 | `dialog-probe-1114-missing-plugin.json` |
| 插件文件损坏（截断到 1 KB） | `--bare` | 中文提示；日志 `启动自检失败：平台插件存在但无法加载（Win32 错误 193）` | `dialog-probe-1114-corrupt-plugin.json` |

安装验证：`dist\Palune-1.0.0-win-x64.exe`（**59,757,564 B**，SHA-256 `6abcb7db0ec814969727938ead5b0ae6fd70e398c0f8f0ec533ce8b893f74278`）静默安装到 `C:\PaluneVerify2`，`bin\qt.conf`、`platforms\qwindows.dll`、`vcruntime140.dll` 均在，无 `install-check.log`；测完 `Uninstall.exe /S`，快捷方式与注册表项已清除，仅 NSIS 自更新副本 `Uninstall.exe` 留下（`_?=` 语义），目录已手工删除。

## 5. 仍未覆盖

- 用户当时实际点了哪一份 exe 无法确认（系统里没有安装副本、没有进程、没有今日错误事件）。若再次出现，按第 1 节四项采集即可定位。
- 安全软件把 DLL 隔离/写入不完整的真实场景仍靠安装器自检 + 人工验收清单 §5。
