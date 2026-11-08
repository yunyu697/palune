# alpha9 · 应用图标落地证据

唯一源：`resources/brand/wave-clay-dark.svg`（用户提供，原样入仓，形状与配色未改）
SHA-256 `B23A761B120A367CD9BDD8C3DB86F0DB063E3D395D3C3180642B50B1DE4E052F`（与附件逐字节一致）。

## 1. 导出链路

- 栅格化：`tools/brand/svg_raster.cpp`（Qt Svg，独立小工具，不进主工程；用法见文件头）。
- 打包：`tools/brand/build_icons.py`（用上面工具出 PNG，再用 Pillow 写 ICO / ICNS / 对照图）。

```
python tools/brand/build_icons.py --rasterizer <svg_raster.exe> --qt-bin C:\Qt\6.8.3\msvc2022_64\bin
```

产物：

| 文件 | 内容 | 字节 | SHA-256 |
| --- | --- | --- | --- |
| `resources/brand/png/palune-16/24/32/48/64/128/256/512/1024.png` | 各尺寸灰度无关的 32bit PNG | — | — |
| `resources/brand/palune.ico` | 16/24/32/48/64/128/256，全部 32bpp DIB 条目（rc.exe 不接受 PNG 条目） | 372,526 | `EE9782C4…3A624A55` |
| `resources/brand/palune.icns` | icp4/icp5/ic07/ic08/ic09 + ic11/ic12/ic13/ic14（16/32/128/256/512 的 1x 与 2x） | 112,018 | `C65CC725…8684C72` |
| `packaging/palune.ico` | 与上同（exe 资源固定路径，脚本生成拷贝） | 372,526 | 同上 |
| `packaging/palune-256.png` | 256px，qrc 里的窗口/托盘/关于页图标 | 9,167 | `E7731D1A…6C652B` |

## 2. 小尺寸（16/24/32）补偿

问题：源文件外框描边 0.5 单位，16px 下仅 0.08 像素 → 整条外框消失，深色任务栏上图标与背景连成一片；
条带在 16px 下宽 1.76px、间隙 0.96px，直接缩小会糊成一团、三条不可分离。

补偿（只对 16/24/32 生效，48 以上直接用源 SVG）：

1. 外框描边改为正好 1 像素并内嵌，外轮廓仍精确落在画布 0..100，**外形不变**；
2. 条带上下边吸附像素栅格（skewX 不改变水平边方向，吸附真实有效）；
3. 条带宽度与间隙取整像素、整体保持居中：16px 为 2px 宽 + 1px 间隙，24px 为 3px + 1px，32px 为 4px + 2px。

未改动：底板颜色 `#0E1A1D`、外框颜色 `#284C58`、三条配色与 22 单位圆角、skewX(-8) 倾斜角度。

## 3. 可辨性核对

- 16px 实际像素 ×16 放大（深色任务栏 `#202020` / 浅色背景 `#F3F3F3` 各一组，附 1:1 真实大小）：
  `docs/evidence/shots/alpha9-icon/icon-16px-zoom.png`
- 三尺寸对照（16/24/32 + 48/64 过渡 + 512/1024 预览）：
  `docs/evidence/shots/alpha9-icon/icon-sizes-compare.png`

结论：两种底色下外框轮廓均可见（浅色背景呈现为深色外框，深色任务栏呈现为亮青外框）；
16px 下三条条带可分离，无粘连。

## 4. 接入点

| 接入点 | 实现 | 文件 |
| --- | --- | --- |
| Windows 可执行文件图标 | `IDI_ICON1 ICON DISCARDABLE "palune.ico"` | `packaging/palune.rc` |
| NSIS 安装器图标 | `CPACK_NSIS_MUI_ICON` | `CMakeLists.txt` |
| 卸载器图标 | `CPACK_NSIS_MUI_UNIICON` | `CMakeLists.txt` |
| 开始菜单 / 桌面快捷方式 | .lnk 不指定图标 → 继承目标 `bin/Palune.exe` 图标（exe 已换新） | NSIS 自动 |
| 卸载项（应用列表） | `CPACK_NSIS_INSTALLED_ICON_NAME = bin\Palune.exe` | `CMakeLists.txt` |
| 窗口标题栏 / 任务栏 | `app.setWindowIcon(:/packaging/palune-256.png)` + `SetCurrentProcessExplicitAppUserModelID` | `src/app/main.cpp` |
| 托盘 | `QSystemTrayIcon`（新增，菜单：显示主窗口 / 退出；自动化截图模式不创建） | `src/app/AppController.cpp` |
| 关于页 | qrc 里 64×64 品牌图 | `qml/SettingsPanel.qml` |
| macOS | `CFBundleIconFile=palune.icns` + bundle Resources 内 `palune.icns` | `packaging/macos/Info.plist.in`、`CMakeLists.txt` |

安装包文件名未改：`Palune-1.0.0-alpha9-win-x64.exe`。

## 5. 实机截图（四处）

| 位置 | 文件 | 说明 |
| --- | --- | --- |
| 任务栏 | `taskbar.png` | 任务栏按钮区，最右为 Palune（深色圆角底 + 三色条带），带活动下划线 |
| 开始菜单 | `start-menu.png` | 搜索 `Palune`：最佳匹配 / 应用 / Uninstall Palune 三处均显示新图标 |
| 安装向导 | `installer-wizard.png` | “Palune 安装”首页，标题栏左上角为新图标 |
| 托盘 | `tray.png` | 通知区内的 16px 图标（浅色任务栏） |

现场事实（如实记录）：

- 本机 3840×2160 @200%（DPA=2）,任务栏为浅色主题。
- Windows 默认把新注册的托盘图标放进“隐藏图标”溢出面板；为抓图，临时把
  `HKCU\Control Panel\NotifyIconSettings\<Palune条目>\IsPromoted` 置 1 并重启 explorer，
  图取到后已删除该值并再次重启 explorer 复原。托盘图是在“常显”状态下拍的，
  图标渲染本身与是否常显无关。
- 溢出面板分页控件在本会话的合成输入下无法稳定翻页，因此托盘证据采用“常显后截图”方式取得，
  未用溢出面板截图。

## 6. 本地包与验证

- 包：`dist/Palune-1.0.0-alpha9-win-x64.exe`，59,601,594 字节
  SHA-256 `6E68FBD4F883D98D20D99BC92C6BB2F2472811C6532D0807A06A61F2FC66CA6F`
- 安装→启动→卸载：`pwsh tools/release/verify_windows_install.ps1 -Installer dist\Palune-1.0.0-alpha9-win-x64.exe -OutDir qa\runtime\alpha9` → PASS
  （证据 `qa/runtime/alpha9/windows-install-evidence.json`、`install-launch.json`）
- 单元测试：ctest 7/7 通过。
- 版本 `VERSION` = `1.0.0-alpha9`；本地 tag `1.0.0-alpha9`（未推送）。
