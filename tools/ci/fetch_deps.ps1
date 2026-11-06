# Windows 本地依赖恢复：把 libmpv / libsodium 运行时库放回 third_party。
# 说明：libmpv 头文件（third_party/libmpv/include/mpv）已入库，编译不需要本脚本；
# 本脚本只负责运行时 DLL，用于本机跑播放与更新 e2e。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools/ci/fetch_deps.ps1 [-Source <已有依赖目录>]
#
# -Source 指向一个已含 libmpv-2.dll / libsodium.dll 的目录（默认 ..\..\_third_party）。
param(
    [string]$Source = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $Source) {
    $Source = Join-Path (Split-Path -Parent $root) "_third_party"
}

$mpvDir = Join-Path $root "third_party\libmpv"
$sodiumDir = Join-Path $root "third_party\libsodium"
New-Item -ItemType Directory -Force -Path $mpvDir, $sodiumDir | Out-Null

function Restore-Item([string]$relative, [string]$target) {
    $path = Join-Path $Source $relative
    if (Test-Path $path) {
        Copy-Item $path $target -Force
        Write-Output "已复制 $relative -> $target"
        return $true
    }
    return $false
}

$mpv = Restore-Item "libmpv-2.dll" (Join-Path $mpvDir "libmpv-2.dll")
$sodium = Restore-Item "libsodium.dll" (Join-Path $sodiumDir "libsodium.dll")

if (-not $mpv) {
    Write-Warning ("缺少 libmpv-2.dll：请从 https://sourceforge.net/projects/mpv-player-windows/files/libmpv/ " +
                   "下载 mpv-dev-x86_64 包并解出 libmpv-2.dll 放到 $mpvDir")
}
if (-not $sodium) {
    Write-Warning ("缺少 libsodium.dll：请从 https://download.libsodium.org/libsodium/releases/ " +
                   "下载 libsodium-1.0.20-RELEASE-VS.zip 解出 libsodium.dll 放到 $sodiumDir")
}

if ($mpv -and $sodium) { Write-Output "依赖就绪" } else { exit 1 }
