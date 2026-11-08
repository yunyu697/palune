# 第三方二进制清单

本目录下的 DLL 不入库（见根 `.gitignore`），只保留 mpv 头文件与这份清单。
任何人凭下表即可复现出与发布包**逐字节一致**的运行时依赖：

```powershell
# 下载并按表中 SHA256 校验，写入 third_party/libmpv 与 third_party/libsodium
pwsh tools/release/fetch_win_runtime.ps1
# 只校验、不落盘
pwsh tools/release/fetch_win_runtime.ps1 -VerifyOnly
```

## 运行时依赖（随安装包分发）

| 组件 | 上游仓库 | tag | 资产 | 资产 SHA256 | 归档大小 | 取出规则 | 文件 | 文件大小 | 文件 SHA256 | 文件内版本 |
|---|---|---|---|---|---|---|---|---|---|---|
| libmpv-2.dll | `shinchiro/mpv-winbuild-cmake` | `20261106` | `mpv-dev-x86_64-20261106-git-9028187538.7z` | `b5a3cc91b5dabf88ad5bb21d000834da4fd0bbda30f4584d39c082127bd62414` | 31,801,743 B | 递归取归档中唯一的 `libmpv-2.dll` | `third_party/libmpv/libmpv-2.dll` | 121,944,064 B | `ccfb18e0b96a58cd36aed0be74284f29d37532ee5a4403a97c0700084239bb97` | `v0.42.0-3-g902818753` |
| libsodium.dll | `jedisct1/libsodium` | `1.0.22-RELEASE` | `libsodium-1.0.22-msvc.zip` | `3e03a726fac4bc09cb61d8f29d658ef7a5eca0811de59082130414f7ca2e4279` | 17,690,194 B | 归档内含 Win32/ARM64/x64、Debug/Release、v142/v143 多份；**只取路径同时匹配 `\x64\` + `\Release\` + `\dynamic\` 的**，多版本时按完整路径降序取第一条（即 v143），绝不按遍历顺序取第一份 | `third_party/libsodium/libsodium.dll` | 462,848 B | `f656aeb789bfc3a2ac587fae1d7cfe278bbe8ce73f28da73b4d08282ea8f9fe3` | `1.0.22.0` |

`third_party/libmpv/include/` 下的头文件来自同一个 mpv-dev 归档（`include/mpv/`），
随仓入库以便 CI 直接编译；升级 libmpv 时头文件需与 DLL 同批更新。

历史说明：2026-11-08 之前本地包里的 `libsodium.dll` 是 1.0.20（349,184 B，
sha256 `f1b56eb5…`），来源未固定、且不在 1.0.22 上游归档中。已按上表刷新为
1.0.22，与 CI 取到同一个文件。

## 未入库、随构建环境提供的依赖

这些不放在 `third_party/`，由构建机或 CI action 提供，**不属于可复现范围**；
下表记录当前实际来源，出现“同版本对不上”时先查这里。

| 组件 | 来源 | 本机实际 | CI 实际 | 一致性 |
|---|---|---|---|---|
| Qt 运行库 | 本机：Qt 在线安装器 `6.8.3/msvc2022_64`；CI：`jurplel/install-qt-action` 安装 `6.9.3` | `Qt6Core.dll` 6.8.3.0（sha256 `0cc62e84…`），由 `windeployqt` 部署 | 6.9.3（`win64_msvc2022_64`） | **不一致（小版本不同）**。CI 用 6.9.3 是因为 macOS 26 SDK 移除了 AGL.framework，Qt 6.8.3 在 macOS 上链接必失败；CMake 只要求 >= 6.8。本地包与 CI 包的 Qt DLL 因此不同，功能等价但不逐字节一致 |
| MSVC 运行库（vcruntime140 / msvcp140 等） | 构建机 VC Redist 目录（`VCINSTALLDIR/Redist/MSVC/*/x64/Microsoft.VC*.CRT`） | 14.51.36247.0（VS 18 Community）；`vcruntime140.dll` sha256 `d1f4225d…` | runner 自带 VS 的 Redist，版本随镜像滚动 | **不保证一致**，也不钉版本；ABI 兼容，属于“机器相关”输入 |
| NSIS / CPack | CMake 自带 CPack + 本机 NSIS | 只影响安装器封装字节，不影响运行文件 | runner 自带 | 不比较 |

## 复核方式

构建后可直接比对（安装树里的值应与上表一致）：

```powershell
Get-FileHash build/Release/libmpv-2.dll, build/Release/libsodium.dll -Algorithm SHA256
```

CI 每次 Windows 构建会把 `Palune.exe` / `libmpv-2.dll` / `libsodium.dll` 的
大小与 SHA256 写入 `build-fingerprint.txt` 并上传为 `windows-build-fingerprint`
工件，可直接与本机结果对照。
