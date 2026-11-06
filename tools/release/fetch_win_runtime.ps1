# 下载 Windows 运行时依赖：libmpv-2.dll（shinchiro 的 mpv-dev 构建）与 libsodium.dll。
# 只用于 release 打包；CI 的常规构建没有这两个文件也能通过编译（运行时动态加载）。
param(
    [string]$Root = ""
)

$ErrorActionPreference = "Stop"
if (-not $Root) { $Root = (Get-Location).Path }

$mpvDir = Join-Path $Root "third_party\libmpv"
$sodiumDir = Join-Path $Root "third_party\libsodium"
New-Item -ItemType Directory -Force -Path $mpvDir, $sodiumDir | Out-Null

$headers = @{ "User-Agent" = "palune-release-ci" }

# ---- libmpv ----
if (-not (Test-Path (Join-Path $mpvDir "libmpv-2.dll"))) {
    Write-Output "下载 libmpv-2.dll ..."
    $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases/latest" -Headers $headers
    $asset = $rel.assets | Where-Object { $_.name -match '^mpv-dev-x86_64-\d' } | Select-Object -First 1
    if (-not $asset) { throw "未找到 mpv-dev-x86_64 资产" }
    $archive = Join-Path $env:RUNNER_TEMP $asset.name
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $archive -Headers $headers
    $extract = Join-Path $env:RUNNER_TEMP "mpv-dev"
    Remove-Item -Recurse -Force $extract -ErrorAction SilentlyContinue
    & 7z x -y "-o$extract" $archive | Out-Null
    $dll = Get-ChildItem -Recurse -Filter "libmpv-2.dll" $extract | Select-Object -First 1
    if (-not $dll) { throw "mpv-dev 包里没有 libmpv-2.dll" }
    Copy-Item $dll.FullName (Join-Path $mpvDir "libmpv-2.dll") -Force
    Write-Output "已就绪 libmpv-2.dll（来源 $($asset.name)）"
} else {
    Write-Output "libmpv-2.dll 已存在"
}

# ---- libsodium ----
if (-not (Test-Path (Join-Path $sodiumDir "libsodium.dll"))) {
    Write-Output "下载 libsodium.dll ..."
    $zip = Join-Path $env:RUNNER_TEMP "libsodium.zip"
    Invoke-WebRequest -Uri "https://download.libsodium.org/libsodium/releases/libsodium-1.0.20-RELEASE-VS.zip" -OutFile $zip -Headers $headers
    $extract = Join-Path $env:RUNNER_TEMP "libsodium"
    Remove-Item -Recurse -Force $extract -ErrorAction SilentlyContinue
    & 7z x -y "-o$extract" $zip | Out-Null
    $dll = Get-ChildItem -Recurse -Filter "libsodium.dll" $extract |
        Where-Object { $_.FullName -match '\x64' } | Select-Object -First 1
    if (-not $dll) { $dll = Get-ChildItem -Recurse -Filter "libsodium.dll" $extract | Select-Object -First 1 }
    if (-not $dll) { throw "libsodium 压缩包里没有 libsodium.dll" }
    Copy-Item $dll.FullName (Join-Path $sodiumDir "libsodium.dll") -Force
    Write-Output "已就绪 libsodium.dll"
} else {
    Write-Output "libsodium.dll 已存在"
}

Write-Output "依赖就绪：$mpvDir / $sodiumDir"
