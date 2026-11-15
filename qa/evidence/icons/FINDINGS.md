# 图标资源平台合规审计（Palune）

日期：2026-11-15
范围：macOS `.icns`、Windows `.ico`、Linux hicolor、托盘/菜单栏、窗口/任务栏图标
方法：直接解析文件字节（ICNS 分块、ICO 目录项、PNG alpha），不依赖肉眼与打包产物
数据：`qa/evidence/icons/icon-audit.json`；对照图 `macos-1024-{before,after}.png`、`win16-8x.png`、`tray-template-{18,36}-8x.png`

## 0. 结论

| 项 | 审计结论 | 处理 |
| --- | --- | --- |
| macOS `.icns` 尺寸集 | 完整：icp4/icp5/ic07/ic08/ic09/ic10 + ic11–ic14（16/32/128/256/512 的 1x 与 2x，含 1024） | 无需补档 |
| macOS 1024 本体 | **不合规**：本体 1024×1024 满画布，不是 Big Sur 的 824×824 居中 | 已按 824/1024 重排全部 10 段 |
| macOS 圆角 | 227px（理论 225），落在满画布上；目标约 185 | 重排后 183px（差 2px / 1.1%） |
| Windows `.ico` | 合规：16/24/32/48/64/128/256，全部 32bpp DIB | 无需改 |
| Windows 16px 清晰度 | 三条声浪在 16px 下仍有 1px 背景间隔，未糊成一团 | 无需改 |
| Linux 图标 | **不合规**：AppImage/deb/rpm 只装 256×256 一档 | 改为 hicolor 8 档 |
| 托盘/菜单栏 | **不合规**：所有平台都用 256 彩色 App 图；macOS 菜单栏没有单色 template | 已分平台：macOS template + `setIsMask(true)`；Windows 16–64 阶梯 |
| 窗口/任务栏 | **降级**：`setWindowIcon` 只给一张 256，系统按 DPI 硬缩 | 改为 16–1024 多尺寸 QIcon |

根因：各入口都直接复用了同一张 256 彩色 App 图，且 ICNS 导出没有做 macOS 图标网格。
不是某处漏设，而是「图标只有一个尺寸/一种形态」这一条贯穿所有入口。

## 1. macOS App 图标

### 1.1 尺寸集（修复前后相同）

| 分块 | 尺寸 | 字节 | 用途 |
| --- | --- | --- | --- |
| icp4 | 16 | 631 | 1x |
| icp5 | 32 | 1,492 | 1x |
| ic07 | 128 | 6,427 | 1x |
| ic08 | 256 | 13,242 | 1x |
| ic09 | 512 | 29,468 | 1x |
| ic10 | 1024 | 67,923 | 1x |
| ic11 | 32 | 1,492 | 16@2x |
| ic12 | 64 | 2,856 | 32@2x |
| ic13 | 256 | 13,242 | 128@2x |
| ic14 | 512 | 29,468 | 256@2x |

10 段齐全，1024 存在；`.icns` 体积 112,018 → 166,329 字节（重排后透明边距增大）。

### 1.2 1024 本体与圆角（alpha≥128 实测）

| | 修复前 | 修复后 | Big Sur 目标 |
| --- | --- | --- | --- |
| 画布 | 1024×1024 | 1024×1024 | 1024×1024 |
| 本体 bbox | (0,0)–(1024,1024) | **(100,100)–(924,924)** | 居中 824×824 |
| 本体尺寸 | 1024×1024 | **824×824** | 824×824 |
| 圆角（拟合） | 227px（理论 225 = 22/100×1024） | **183px** | 约 185px |

各档本体尺寸均按 824/1024 等比落位：

| 画布 | 16 | 32 | 64 | 128 | 256 | 512 | 1024 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 本体实测 | 13 | 26 | 52 | 103 | 206 | 412 | 824 |

修复前后 1024 对照图：`qa/evidence/icons/macos-1024-before.png`（满画布）、
`macos-1024-after.png`（824 居中，四周 100px 透明）。

不需要重绘。唯一残差是圆角 183 vs 185（2px）：源 SVG 的圆角是 22 单位，按 824 缩放后得
181.3；若要与 Apple 模板逐像素一致，可把源圆角从 22 调到 22.5（属美术参数微调，本次未改）。

## 2. Windows `.ico`

| 尺寸 | 位深 | 字节 | alpha bbox | 唯一颜色数 |
| --- | --- | --- | --- | --- |
| 16 | 32bpp | 1,128 | 全画布 | 65 |
| 24 | 32bpp | 2,440 | 全画布 | 81 |
| 32 | 32bpp | 4,264 | 全画布 | 134 |
| 48 | 32bpp | 9,640 | 全画布 | 181 |
| 64 | 32bpp | 16,936 | 全画布 | 248 |
| 128 | 32bpp | 67,624 | 全画布 | 465 |
| 256 | 32bpp | 270,376 | 全画布 | 675 |

`.ico` 总大小 372,526 字节，重打包后与修复前一致（内容未退化）。

16px 像素分类（`B` 底板、`Y/O/R` 三条声浪、`?` 抗锯齿、`.` 透明）：

```
..????????????..
.?BBBBBBBBBBBB?.
?BBBBBBBBBBBBBB?
?BBB?Y?BBBBBBBB?
?BBB?Y??O?BBBBB?
?BBB?Y??O?BBBBB?
?BBBYY??O??OBBB?
?BBBYYBOOBOOBBB?
?BBBYYBOOBOOBBB?
?BB?YY?O?BO?BBB?
?BB?Y??O?BBBBBB?
?BB?Y??O?BBBBBB?
?BB?Y?BBBBBBBBB?
?BBBBBBBBBBBBBB?
.?BBBBBBBBBBBB?.
..????????????..
```

第 7/8 行连续区段：

```
B(1-3) Y(4-5) B(6) O(7-8) B(9) O(10-11) B(12-14)
```

三条声浪之间各有 1px 背景色，没有并成色块；16px 判定清晰。放大图：`win16-8x.png`、`win32-8x.png`。

## 3. Linux

| | 修复前 | 修复后 |
| --- | --- | --- |
| AppDir hicolor | 只有 `256x256/apps/palune.png` | `16/24/32/48/64/128/256/512` 八档 |
| deb 根 | 只有 256 | 八档 |
| rpm `%files` | `/usr/share/icons/hicolor/256x256/apps/palune.png` | `/usr/share/icons/hicolor/*/apps/palune.png` |

源文件 `resources/brand/png/palune-{16,24,32,48,64,128,256,512,1024}.png` 全部存在（上表已实测）。
改动在 `tools/release/package_linux.sh`，尺寸列表由 `ICON_SIZES` 单一变量控制。

**未验证**：本机是 Windows，未实际生成 AppImage/deb/rpm，也未在 Linux 上验证图标缓存命中；
需要一台 Linux 构建机跑一次出包并 `ls` hicolor 目录。

## 4. 托盘 / 菜单栏

修复前（`src/app/AppController.cpp`）：

```cpp
m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/packaging/palune-256.png")), this);
```

所有平台共用一张 256 彩色 App 图。macOS 菜单栏把彩色 App 图标当状态项显示，系统不会反色，
与 Shellora 被指出的问题同类。

修复后（`src/app/BrandIcon.h`）：

```cpp
#if defined(Q_OS_MACOS)
    icon.addFile(":/resources/brand/png/tray-template-18.png", QSize(18, 18));
    icon.addFile(":/resources/brand/png/tray-template-36.png", QSize(36, 36));
    icon.setIsMask(true);   // macOS 按菜单栏明暗自动反色
#else
    return brandIcon({16, 24, 32, 48, 64});   // Windows 托盘实际用 16/20/24/32
#endif
```

template 资产由 `tools/brand/build_icons.py` 从同一份 SVG 的声浪几何生成（无底板、无外框、纯黑 + alpha）：

| 文件 | 画布 | alpha bbox | 非黑 RGB 像素 | 用途 |
| --- | --- | --- | --- | --- |
| `tray-template-18.png` | 18×18 | (2,1)–(16,17) | 0 | 1x 菜单栏 |
| `tray-template-36.png` | 36×36 | (5,1)–(31,35) | 0 | @2x 菜单栏 |

「非黑 RGB 像素 0」表示所有可见像素都是纯黑，只有 alpha 变化，符合 template 图要求。
放大图：`tray-template-18-8x.png`、`tray-template-36-8x.png`。

## 5. 窗口 / 任务栏 / exe

| 入口 | 修复前 | 修复后 |
| --- | --- | --- |
| `app.setWindowIcon` | 单张 `:/packaging/palune-256.png` | `brandAppIcon()`：16/24/32/48/64/128/256/512/1024 九档 |
| Windows exe 资源 | `packaging/palune.rc` → `palune.ico` | 不变（ico 已含 7 档） |
| NSIS 安装器/卸载器 | `CPACK_NSIS_MUI_ICON` → 同一 ico | 不变 |
| macOS bundle | `CFBundleIconFile=palune.icns` | 不变（icns 已重排网格） |
| Linux 桌面项 | `Icon=palune` | 不变（hicolor 八档） |

qrc 已收录全部品牌 PNG 与两张 template（构建产物 `build/.qt/rcc/qrc_app_resources.cpp` 可核对）。

## 6. 修改文件

| 文件 | 变更 |
| --- | --- |
| `tools/brand/build_icons.py` | 新增 Big Sur 网格重排（824/1024）、template 生成、`--repack`；ICNS 写盘改用重排后的画布 |
| `resources/brand/palune.icns` | 重生成，10 段全部 824 本体居中 |
| `resources/brand/png/tray-template-{18,36}.png` | 新增单色 template |
| `src/app/BrandIcon.h` | 新增窗口/托盘图标入口，按平台分形态 |
| `src/app/main.cpp` | 窗口图标改多尺寸 |
| `src/app/AppController.cpp` | 托盘图标改用 `brandTrayIcon()` |
| `CMakeLists.txt` | qrc 收录 9 档 PNG + 2 张 template |
| `tools/release/package_linux.sh` | hicolor 八档；rpm `%files` 改通配 |
| `docs/evidence/UI-ALPHA9-ICON.md` | 追加本次修订记录 |

## 7. 未验证（不计入通过）

1. macOS 真机 Dock 观感与同排应用是否一致（本机无 macOS，只验证了画布像素）。
2. macOS 菜单栏 template 在浅色/深色菜单栏下的实际反色效果（`setIsMask` 代码路径与资产已就绪）。
3. Windows 托盘在 125%/150%/200% DPI 下系统实际取哪一档、是否清晰（只验证了 ico/PNG 档位齐全）。
4. Linux AppImage/deb/rpm 实际出包与 hicolor 命中（需 Linux 构建机）。
5. `.icns` 被 macOS 的 `iconutil`/Finder 接受与缓存刷新（无法在 Windows 验证）。
6. 通知区图标在 Windows“隐藏图标”溢出面板中的显示（需真实桌面操作，硬约束禁止抢前台）。

未触碰 Shellora 仓库；本次所有改动均在 `palune` 内。
