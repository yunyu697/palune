# qwindows.dll 找不到：三种情况的结论与证据

日期：2026-11-13
机器：Windows 10/11 x64（本机），Qt 6.8.3 msvc2022_64，Palune `1.0.0-RC`
样本：`dist/Palune-1.0.0-win-x64.exe`（59,725,136 B，SHA-256 `d2131369…7320a3`，与 `dist/release-1.0.0-rc/` 同一份）

> 排查全程离屏/隐藏桌面：不抢焦点、不弹模态、不播音频。需要真实窗口的检查集中在一次批处理里执行（见 `tools/m0/run_hidden_selftest.ps1`）。

## 情况 ①：安装目录里是否真的缺 `platforms\qwindows.dll`

**不缺。** 用 7-Zip 以只读方式解开安装器（不执行安装器，不经过 UAC）：

| 包 | `bin\platforms\qwindows.dll` |
| --- | --- |
| `dist/Palune-1.0.0-win-x64.exe` | 存在 |
| `dist/release-1.0.0-rc/Palune-1.0.0-win-x64.exe` | 存在 |
| `dist/release-1.0.0-deps-refresh/Palune-1.0.0-win-x64.exe` | 存在 |

包内插件与 Qt SDK 源文件完全一致（同一 x64 构建）：

```
bin\platforms\qwindows.dll   907,912 B   sha256 12577a7c4f2230bbea53f279573d7e3d1a08d631197855481e1e1580f61bb877
C:\Qt\6.8.3\msvc2022_64\plugins\platforms\qwindows.dll      同上
bin\Qt6Core.dll              6,159,496 B sha256 0cc62e841959fb8f6d366ea116c6f6bbc273e30f0ef90d8aa2e4adeb81fd63f8
C:\Qt\6.8.3\msvc2022_64\bin\Qt6Core.dll                     同上
```

安装器里**没有 `qt.conf`**（本轮已补，见 `packaging/qt.conf`）。安装树结构为 `bin\Palune.exe` + `bin\platforms\…` + `bin\qml\…`，符合 windeployqt 布局。

## 情况 ②：文件在但加载失败

- **依赖缺失**：不成立。`dumpbin /dependents` 显示 `qwindows.dll` 只依赖随包 DLL（`Qt6Gui.dll`、`Qt6Core.dll`、`MSVCP140.dll`、`VCRUNTIME140.dll`、`VCRUNTIME140_1.dll`，均在 `bin\`）与系统 DLL（`USER32`/`GDI32`/`dwmapi`/`d3d9`/`UIAutomationCore` 等）。
- **位数不符**：不成立。`dumpbin /headers` 为 `8664 machine (x64)`，与 `win-x64` 安装包一致。
- **被安全软件隔离 / 写入失败**：**这是唯一能在用户环境里成立的“文件在但没了”路径**，本机无法安全制造隔离，转入 `docs/manual-acceptance.md` §5；安装器已加落盘自检来覆盖它。
- **`QT_PLUGIN_PATH` 被外部环境变量污染**：实测**不足以**让对方启动失败。把 `QT_PLUGIN_PATH` 指向另一套 Qt 插件、把 `QT_QPA_PLATFORM_PLUGIN_PATH` 指向空目录，部署版仍能启动（Qt 6.8 仍会用到应用目录）。
  真正的机制是：**部署版的 `Qt6Core.dll` 就在 exe 旁，Qt 的插件前缀由这个 DLL 的位置推导（= exe 目录），不会再回落到开发机的 `C:\Qt\…`**。因此只要 `bin\platforms\` 不在，Qt 就必然 `qFatal`，与外部环境变量无关。

结论：② 的实际内容是“**文件不在**（安装不完整/被隔离）”，不是“文件在却加载不了”。

## 情况 ③：只在特定启动方式下失败

- **快捷方式工作目录 / 从别处 cwd 拉起**：不成立。安装树在 `WorkingDir=C:\Windows\System32` 启动、开发树在 `C:\Windows\System32` 启动均通过（见下方矩阵）。Qt 6 用的是 exe 所在目录，不是 cwd。
- **被其它程序以“没有部署插件”的目录拉起**：**成立，且本机有实证**。WER 报告 `AppCrash_Palune.exe` 记录的崩溃进程全部是
  `D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune\build\Release\Palune.exe`，
  其中 2026-11-13 16:47 那份（`c0000409`，`Qt6Core.dll`）的 `LoadedModule` 列表里**没有任何 qwindows.dll**——即进程在 `QApplication` 构造阶段就 `qFatal` 了。
  这正是“日常直接运行构建产物”的路径：`build\Release\` 里没有 `platforms\`，只能靠外部 Qt 前缀，一旦前缀对不上就弹英文框。

## 启动矩阵（本轮实测）

| # | 被启动的对象 | 启动条件 | 结果 |
| --- | --- | --- | --- |
| A | 安装包解包树 | `cwd=bin`，干净环境 | PASS |
| B | 安装包解包树 | `cwd=C:\Windows\System32` | PASS |
| C | 安装包解包树 | `QT_QPA_PLATFORM_PLUGIN_PATH=`空目录 | PASS（启动自检清理该变量） |
| D | 安装包解包树 | `QT_PLUGIN_PATH=C:\Qt\6.8.3\msvc2022_64\plugins` | PASS |
| E | 上一个包（11-08） | `cwd=bin` | PASS |
| F | 上一个包（11-08） | `QT_QPA_PLATFORM_PLUGIN_PATH=`空目录 | PASS |
| G | 去掉 `platforms\` 的安装树 | 把 `QT_QPA_PLATFORM_PLUGIN_PATH` 指向有效 `platforms` | PASS（证明“钉住路径”有效） |
| H | 构建树 `build\Release`（本轮修复后自带部署） | `cwd=C:\Windows\System32` + 空 QPA 目录 | PASS |
| I | 本轮安装树（带 `qt.conf`） | `cwd=C:\Windows\System32` | PASS |
| J | 去掉 `platforms\` 的安装树 | 干净环境 | 中文提示 + `[ERROR]` 日志（见 `docs/evidence/dialogs/`） |

批量执行：`tools/m0/run_hidden_selftest.ps1`（独立桌面，不占前台）+ `qa/runtime/probe_qt_dialogs.py`（仅用于 J 这种“只有对话框、没有主窗口”的用例，对话框自动移到屏幕外后离屏回读）。

## 本轮修复（分发级）

1. **构建树自带部署**（`CMakeLists.txt` POST_BUILD windeployqt）：`build\Release\Palune.exe` 现在可直接双击启动，覆盖情况 ③。
2. **`bin\qt.conf` 随包安装**：`Prefix=. Plugins=. Qml2Imports=qml Translations=translations`，插件/QML 解析钉死在安装目录。
3. **打包门禁**：`windeployqt` 缺失时 CMake 直接 `FATAL_ERROR`（以前静默跳过会打出没有 Qt 运行时的包）；安装阶段断言 `Palune.exe`/`Qt6Core.dll`/`Qt6Gui.dll`/`platforms/qwindows.dll`/`qt.conf`/`qml/QtQuick/qmldir` 存在；`package_windows.ps1` 再次断言 11 项运行时组件。
4. **安装器落盘自检**（`CPACK_NSIS_EXTRA_INSTALL_COMMANDS`）：安装结束前检查 5 个关键文件，缺失时中文提示 + 写 `install-check.log` + 退出码 3，覆盖情况 ②（安全软件拦截/写入不完整）。
5. **启动自检修正**：只认应用目录里的插件，不再把开发机 `QLibraryInfo` 前缀算作“可用”（上一版会在开发机上误判通过，真正启动才 `qFatal`）。
