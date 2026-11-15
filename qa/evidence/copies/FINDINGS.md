# Palune.exe 副本归属定案

证据：`report.md`（22 个副本逐个真启动的原始结果）、`result.json`、`loader-chain.json`（受控复现）、
`dialog-queue.json`（桌面上排队错误框逐条读出）、`/qa/evidence/dialogs/loader-dll-not-found-qt6quick.png`（实拍）。

## 一、这台机器上到底有几个副本

全盘枚举（`%LOCALAPPDATA%\Programs`、`%LOCALAPPDATA%\Temp`、`Downloads`、`dist`、所有构建树、`C:\PaluneVerify*`、`_CPack_Packages`）共 **22 个 `Palune.exe`**，分三类：

| 类型 | 数量 | 副本 | 启动结果 |
| --- | --- | --- | --- |
| A. 部署完整（Qt 全套 + `platforms\qwindows.dll`） | 12 | `build\Release`、`build\stage-record\bin`、`build\_CPack_Packages\...\NSIS\Palune-1.0.0-RC1\bin`、`Temp\palune-final\bin`、`palune-final2\bin`、`palune-install-check\bin`、`palune-pkg-current\bin`、`palune-pkg-old\bin`、`qa\runtime\install-tree\bin`、`_CPack_Packages\...\alpha4\bin`、`build\_CPack_Packages\...\WIX\bin` | 正常起来（覆盖窗 `Palune` + 视频窗 `Palune Video Surface`），6s 后被杀 |
| B. 有 Qt6 DLL、**缺 `platforms\qwindows.dll`** | 4 | `Temp\palune-before`、`palune-noplugin3`、`palune-pkg-noplugins\bin`、`palune-install-noplugins\bin` | 进程活着但不出窗口，弹 `#32770` 标题 **`Palune`** → Qt 自己的「no Qt platform plugin could be initialized / Available platform plugins are: windows」 |
| C. **完全无 Qt**（裸 exe） | 6 | `build-m2\Release`、`build-m2b\Release`、`qa\runtime\soak-bin`、`Temp\palune-solo`、`Temp\palune-noplugins`、`Temp\palune-noplugin2` | 退出码 **`0xC0000135`（STATUS_DLL_NOT_FOUND）**，无窗口，由 csrss 弹「`Palune.exe - 系统错误`：由于找不到 Qt6Xxx.dll」 |

启动方式统一为「工作目录 = exe 所在目录」，与资源管理器双击一致；每个副本跑在独立隐藏桌面（`CreateDesktop`）上，不抢前台。

## 二、用户看到的两个对话框分别属于谁

| 对话框 | 宿主 | 判定 |
| --- | --- | --- |
| `This application failed to start because no Qt platform plugin could be initialized… Available platform plugins are: windows` | 进程自己活着的 **B 类副本**（Qt 已加载，插件初始化失败） | 属 **B 类**：Qt 找到插件目录但初始化失败。本机 4 个 B 类副本全部稳定复现（标题 `Palune`，`#32770`） |
| `Palune.exe - 系统错误：由于找不到 Qt6Quick.dll…` | **csrss.exe（pid 608）**，真正的 Palune 进程在弹框前已退出 | 属 **C 类**：Windows 加载器硬错误（`STATUS_DLL_NOT_FOUND`），不是 Qt 弹的 |

判定依据：`GetWindowThreadProcessId` 对这两个框返回的都是 608（csrss），这是加载器硬错误的特征；
B 类的 `#32770` 宿主是 Palune 自己。实拍与 UIA 原文见 `qa/evidence/dialogs/`。

## 三、「找不到 Qt6Quick.dll」能不能复现

**现存 22 个副本里没有一个是「有 Qt6Widgets、没有 Qt6Quick」的半部署目录**，所以那句话**不能**由现存副本原样复现（记为「未复现」）。

但可以做受控复现，证明这一类对话框的成因（`tools/qa/probe_loader_dialog.ps1`）：

| 变体（从 `build\Release` 复制后只删指定项） | 退出码 | 对话框原文 |
| --- | --- | --- |
| 删 `Qt6Quick.dll` | `0xC0000135` | **「由于找不到 Qt6Quick.dll，无法继续执行代码。重新安装程序可能会解决此问题。」**（与用户截图逐字一致） |
| 删 `Qt6Widgets.dll` | `0xC0000135` | 「由于找不到 Qt6Widgets.dll…」 |
| 删全部 Qt6*.dll | `0xC0000135` | 「由于找不到 Qt6Gui.dll…」（本次运行报的是 Gui） |

- 能证明：**任何**一次部署只缺一个 Qt DLL，就会产生这条一模一样的原生对话框；缺失项的名字就是被删的那个 DLL。
- 不能证明：用户那次点的是哪一个副本。对话框由 csrss 持有，进程已退出，事后无法从对话框反查 pid/路径。

## 四、「关掉一个又弹一个、每次缺的组件不同」是怎么来的

`tools/qa/drain_dialog_queue.ps1` 把桌面上排队的硬错误框逐条读出来并关闭，一次共读出 **19 个**，全部宿主 csrss（pid 608），
标题涵盖 `Palune.exe`、`tst_version.exe`、`tst_playqueue.exe`，缺失项涵盖 `Qt6Gui / Qt6Qml / Qt6Quick / Qt6Widgets`（明细见 `dialog-queue.md`）。

机制：
1. Windows 同一时刻**只显示一个**硬错误框，其余排队；关掉一个才露出下一个。
2. 每个失败的启动只产生一个框，报的是它那一次第一个解析不到的导入项。
3. 同一个全裸目录连续启动多次，报的 DLL 也会变（本轮 4 次得到 Gui / Qml / Quick / Widgets 四种），
   因为加载器解析导入的顺序不保证稳定。
→ 用户看到的「链式、每次不同」= **多次失败的启动 + 一张张排队显示**，不是一个进程连报多次。

## 五、哪些副本会被用户点到（MuiCache 证据）

`HKCU\...\MuiCache` 记录的是**真被 shell 启动过**的 exe。其中仍在磁盘上的有：

- `build\Release\Palune.exe`（A 类，正常）
- `qa\runtime\install-tree\bin\Palune.exe`（A 类，正常）
- `build-m2\Release\Palune.exe`（**C 类裸 exe**）
- `Temp\palune-solo\Palune.exe`（**C 类裸 exe**）
- `Temp\palune-noplugin3\Palune.exe`、`Temp\palune-before\Palune.exe`（**B 类**，弹「no Qt platform plugin」）
- `Temp\palune-pkg-old\bin`、`palune-pkg-noplugins\bin`、`palune-install-noplugins\bin`（A/B 类）
- 已删除目录：`C:\PaluneVerify\bin`、`C:\PaluneVerify2\bin`、`C:\Temp\palune-rc-install\bin`、`dist\preflight-corrupt-plugin`

即：**用户点到的极可能是我此前 QA 留下的裸副本**（`build-m2\Release`、`Temp\palune-solo`）或 B 类半部署目录。
本轮没有删除任何文件；建议（需用户确认后执行）：
- `Temp\palune-*`（7 个）、`C:\PaluneVerify*`：历史 QA 残留，可整体删除。
- `build-m2\Release`、`build-m2b\Release`、`qa\runtime\soak-bin`：裸 exe 目录，**保留但不建议双击**；第 3 条会从构建侧让这种目录不再产生。

## 七、口径订正：9 与 22 的差集（撤回「9 个」这个总数）

上一轮报的「全盘现存 9 个」**口径错了**，撤回。两次枚举的命令与命中集合如下（都在 2026-11-15 复核过）：

**上一轮口径（仓库内，不含用户 Temp）**

```powershell
cd palune
$roots = @('build','build-m2','build-m2b','qa','dist','_scratch')
Get-ChildItem $roots -Filter Palune.exe -Recurse -File
Get-ChildItem '..\_CPack_Packages' -Filter Palune.exe -Recurse -File
# → 9 个
```

命中 9 个：`build\Release`、`build\stage-record\bin`、`build\_CPack_Packages\...\NSIS\Palune-1.0.0-RC1\bin`、
`build\_CPack_Packages\...\WIX\...\bin`、`build-m2\Release`、`build-m2b\Release`、`qa\runtime\install-tree\bin`、
`qa\runtime\soak-bin`、`..\_CPack_Packages\...\alpha4\bin`。

**本轮口径（仓库内 + 用户可双击到的地方）**

```powershell
$roots = @('build','build-m2','build-m2b','qa','dist','_scratch','..\_CPack_Packages',
           "$env:LOCALAPPDATA\Programs", "$env:LOCALAPPDATA\Temp", "$env:USERPROFILE\Downloads",
           'C:\PaluneVerify','C:\PaluneVerify2','C:\Temp\palune-rc-install',
           'C:\Program Files\Palune','C:\Program Files (x86)\Palune')
Get-ChildItem $roots -Filter Palune.exe -Recurse -File
# → 22 个（多出的 13 个全部在 %LOCALAPPDATA%\Temp\palune-*）
```

**差集 = 13 个，逐个列出（`$env:LOCALAPPDATA\Temp` 下）**

`palune-before`、`palune-final\bin`、`palune-final2\bin`、`palune-install-check\bin`、`palune-install-noplugins\bin`、
`palune-noplugin2`、`palune-noplugin3`、`palune-noplugins`、`palune-pkg-current\bin`、`palune-pkg-noplugins\bin`、
`palune-pkg-old\bin`、`palune-preflight`、`palune-solo`。

结论：
- **正确总数是 22**（枚举时刻 11:08）。「9」漏掉了 `%LOCALAPPDATA%\Temp`，而用户恰恰最可能双击到那里 ——
  MuiCache 里 `Temp\palune-solo`、`Temp\palune-noplugin3`、`Temp\palune-before`、`Temp\palune-preflight` 都出现过。
- 去重口径：按完整路径去重；已检查全部副本的 `ReparsePoint` 属性，**符号链接/联接点 0 个**，
  所以 22 个路径 = 22 份独立文件，不存在链接重复计数。
- 本轮此后又新建了 4 个受控复现目录 `qa\runtime\repro-loader\{none,no-quick,no-widgets,no-qwindows}`，
  它们是从 `build\Release` 复制出来的实验品，**不计入副本清单**，结论仍以 22 为准。


## 八、结论（一句话）

用户遇到的弹窗命中 **B 类（插件初始化失败）和 C 类（加载器缺失依赖）两种**；
根因都不是「Qt 找不到插件目录」，而是**副本本身不完整**（C 类连 Qt 都没有，B 类缺 `platforms\qwindows.dll`），
而这类不完整副本来自构建树 / 打包暂存 / QA 临时目录被直接双击。
「每次缺的组件不同」是多次失败启动的排队显示，不是单进程行为。
