# 启动期错误弹窗与静默失败：全类排查（2026-11-15）

## 1. 结论

- 第三次“Qt 报错弹窗”对应的**本次真正故障**是界面文件回归：`qml/SettingsPanel.qml`
  把 `font.family` / `font.weight` 写在 `OutlinedText`（Item 派生）上。QML 编译期不报错，
  运行到创建窗口时才失败，整个 `Main.qml` 无法实例化，进程以退出码 4 静默结束。
  证据：`%APPDATA%\Palune\logs\palune.log`
  `2026-11-15T09:27:55.717 [ERROR] QML 加载失败，无法启动窗口`。
- 该路径在修改前**没有任何提示**，属于“闪退无提示”。现在：启动失败会给中文说明 + 日志路径。
- 之前出现过的英文 Qt 弹窗来自两类历史原因，均已从根因上处理：
  1. 旧副本（无启动预检）在 `QT_PLUGIN_PATH`/PATH 被外部 Qt 干扰时，由 Qt 自己 qFatal
     弹出英文框（可复现证据见 `dialog-probe-pkg-old-polluted.json`）。
  2. `QApplication` 构造阶段的 qFatal 走 Qt 默认处理器，在 Windows 上必然弹原生英文框。
     现在在 `QApplication` 之前安装消息处理器，Qt 不再有机会弹框。
- 现在“缺件”不再留到运行时：安装器在安装结束时真跑一次 `Palune.exe --version`，
  失败即报错并写 `install-check.log`；CI/发布脚本还会跑安装树 QML 自检。

## 2. 本机可被双击的副本（全量）

搜索范围：项目树、`%LOCALAPPDATA%\Programs`、`%TEMP%`、`C:\PaluneVerify*`、
Program Files、下载目录、开始菜单/桌面快捷方式、卸载注册表项。

| 副本 | 构建时间 | platforms/qwindows.dll | qt.conf | Qt6Core.dll | 启动预检 | 结论 |
| --- | --- | --- | --- | --- | --- | --- |
| `build\Release\Palune.exe` | 11-15 09:53 | 有 | 有 | 有 | 有 | 当前开发树，双击正常 |
| `build\stage-record\bin\Palune.exe` | 11-14 09:11 | 有 | 有 | 有 | 有 | 安装布局副本，正常 |
| `build\_CPack_Packages\...\NSIS\Palune-1.0.0-RC1-win-x64\bin\Palune.exe` | 11-14 09:11 | 有 | 有 | 有 | 有 | 打包中间树，正常 |
| `build\_CPack_Packages\...\WIX\Palune-win-x64-msi\bin\Palune.exe` | 11-08 10:28 | 有 | 无 | 有 | **无** | 旧树；本机可启动，但无预检保护 |
| `qa\runtime\install-tree\bin\Palune.exe` | 11-07 06:28 | 有 | 无 | 有 | **无** | 旧安装树；本机可启动，但无预检保护 |
| `build-m2\Release\Palune.exe` | 11-07 08:14 | 无 | 无 | 无 | 无 | 缺全部 Qt，双击只会静默失败 |
| `build-m2b\Release\Palune.exe` | 11-07 10:04 | 无 | 无 | 无 | 无 | 同上 |
| `qa\runtime\soak-bin\Palune.exe` | 11-07 07:14 | 无 | 无 | 无 | 无 | 同上 |

没有安装到 Program Files / `%LOCALAPPDATA%\Programs` 的副本，也没有卸载注册表项和开始菜单
快捷方式；`%TEMP%` 下另有 13 个历史取证副本（`palune-*`），同样不是用户入口。

**未删除任何用户目录文件。** 支撑 `build-m2`、`build-m2b`、`qa\runtime\soak-bin`
三个目录可以直接删掉，避免误双击；`WIX` / `install-tree` 两份旧树建议保留前先确认不再需要。

## 3. 启动失败类型清单（全类）

| # | 类型 | 触发条件 | 用户会看到 | 现状 |
| --- | --- | --- | --- | --- |
| 1 | 平台插件缺失/加载失败 | 包内缺 `platforms\qwindows.dll` 或被安全软件隔离 | 旧版：Qt 英文框；现版：中文说明 + 退出 | 已消除（启动预检 + 安装器缺件检查 + 安装后 `--version` 实测） |
| 2 | 平台插件初始化失败 | 插件在、初始化异常（显卡/会话异常） | Qt qFatal 英文框 | 已消除（`QApplication` 前接管 Qt 消息处理器，改中文 + 日志） |
| 3 | 依赖 Qt DLL 缺失/被抢占 | PATH 上有另一套 Qt、或安装不完整 | 旧版：英文框/静默退出 | 已消除（`SetDefaultDllDirectories` 只认应用目录+System32，并从 PATH 剔除其它 Qt 目录） |
| 4 | QML 结构性错误 | 属性名写错、类型不存在（本次事故） | 旧版：静默退出码 4；现版：中文说明 + 日志行号 | 已消除（`qmllint` 门禁 + `--qml-check` + 安装树自检） |
| 5 | QML 模块/导入缺失 | 包内 `qml\` 未部署或版本不匹配 | 空白窗口或静默退出 | 已消除（安装器检查 `qml\QtQuick`，CI 安装树自检跑 `--qml-check`） |
| 6 | libmpv 缺失/损坏 | `libmpv-2.dll` 不在或依赖不全 | 旧版：窗口能开但无法播放（静默） | 已消除（启动时检测并中文说明 + 退出；安装器 `--version` 实测） |
| 7 | 图形后端初始化失败 | 显卡驱动/RHI 异常 | Qt qFatal 英文框 | 已消除（统一中文 + 日志；无法在应用层规避驱动问题） |
| 8 | 设置文件损坏 | `settings.toml` 非法 | 无提示，回退默认 | 保持静默回退，已写日志（可接受） |
| 9 | 安装期缺件 | 安全软件拦截/磁盘异常 | 旧版：装完打不开 | 已消除（安装器文件清单检查 + 真跑 `--version` + 失败写 `install-check.log`） |
| 10 | 旧副本误导 | 用户双击历史构建产物 | 缺 DLL 时静默失败 | 不可控（见第 2 节清单；已在文档标注） |

## 4. 改动

| 文件 | 改动 |
| --- | --- |
| `qml/components/OutlinedText.qml` | 新增 `fontFamily` / `fontWeight`，下发到描边与前景 Text |
| `qml/SettingsPanel.qml` | 字幕预览改用 `fontFamily` / `fontWeight`，不再给 Item 赋 `font.*` |
| `qml/Main.qml` | `Qt.SizeDiagCursor` → `Qt.SizeBDiagCursor`（qmllint 发现的另一处不存在属性） |
| `src/app/main.cpp` | `QApplication` 之前安装 Qt 消息处理器（fatal 改中文 + 日志）；`SetDefaultDllDirectories`；从 PATH 剔除其它 Qt 目录；新增 `--qml-check` |
| `src/app/AppController.{h,cpp}` | 新增 `--qml-check`（逐个加载 qrc 内全部 QML，失败退出码 2）；启动失败/`libmpv` 失败给出中文说明 + 日志路径 |
| `src/diag/Log.cpp` | 运行期 `QtFatalMsg` 不再静默 abort，给中文说明 + 日志路径 |
| `CMakeLists.txt` | 新增 `qml_check` 测试；安装器增加 Qt6Widgets/Qt6Qml/Qt6Quick/QuickControls2/qml 目录检查与 `ExecWait ... --version` 实测 |
| `tools/release/check_qml.ps1` | 新增 `qmllint` 结构性门禁（`missing-property`/`unresolved-type`/`syntax`/`compiler` 失败） |
| `tools/release/verify_windows_install.ps1` | 安装后追加 `--qml-check` 与新增必需文件；任一失败则整体 FAIL |
| `.github/workflows/ci.yml`、`release.yml` | 构建后运行 QML 静态门禁 |
| `qa/runtime/probe_qt_dialogs.py` | 新增 `--shell`（ShellExecuteEx，等价资源管理器双击），可捕获加载器阶段的对话框 |

## 5. 验证

工具：`qa/runtime/probe_qt_dialogs.py`（`--shell` 等价双击，工作目录 = exe 所在目录；
窗口一出现即移出屏幕，不抢焦点）。证据：本目录下 `dialog-probe-p0-*.json` 与 `*.png`。

| 启动方式/场景 | 结果 |
| --- | --- |
| 当前构建，ShellExecute 双击（无外部 Qt） | 无对话框；主窗 + 视频窗正常；stderr 为空 |
| 当前构建，PATH 含另一套 Qt（`C:\Qt\6.8.3\...\bin`） | 无对话框；正常启动 |
| 当前构建，`QT_QPA_PLATFORM=bogus` | 无对话框；正常启动（预检清除了无效平台请求） |
| 缺 `platforms\qwindows.dll` | 中文提示“缺少运行所需的组件”，无 Qt 英文框（`dialog-probe-p0-case-noplugin.json`） |
| 缺 `libmpv-2.dll` | 中文提示“播放组件 libmpv 加载失败”，无 Qt 英文框（`dialog-probe-p0-case-nompv2.json`） |
| 旧副本（无预检）+ 外部 Qt 污染 | 会出 Qt 英文框（历史复现，`dialog-probe-pkg-old-polluted.json`）；当前构建不再命中 |
| `Palune.exe --qml-check` | 21 个 QML 全部通过，退出码 0 |
| `tools/release/check_qml.ps1` | 通过；对含错误属性的样例返回 2（已自测） |
| 单元测试（version/manifest/core/actions/preferences/settings/media/theme/media_server） | 全部退出码 0 |

## 6. 剩余风险

- `build-m2`、`build-m2b`、`qa\runtime\soak-bin` 三个目录缺 Qt 运行时，双击会静默失败；
  属于历史构建产物，应用侧无法修复，只能清掉或在文档中标注。
- 显卡驱动导致的 RHI 初始化失败无法在应用层避免，但现在会给出中文说明与日志，不再弹英文框。
- 安装器 `--version` 检查在 SYSTEM 会话下的静默部署中同样有效（只加载器，不建窗口、不建桌面）。
