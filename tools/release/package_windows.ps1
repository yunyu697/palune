# Windows 打包：NSIS 安装器（.exe）+ MSI，产物统一收进 dist/。
param(
    [string]$BuildDir = "build",
    [string]$OutDir = "dist",
    [string]$CMakeExe = "cmake"
)

$ErrorActionPreference = "Stop"
$version = (Get-Content VERSION -Raw).Trim()
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outFull = (Resolve-Path $OutDir).Path

Push-Location $BuildDir
try {
    Write-Output "== CPack NSIS =="
    & cpack -G NSIS -C Release --config .\CPackConfig.cmake
    if ($LASTEXITCODE -ne 0) { throw "NSIS 打包失败（退出码 $LASTEXITCODE）" }

    # MSI 需要 WiX（candle/light）。本机未安装 WiX 时跳过，不阻塞 NSIS 安装包。
    $haveWix = $null -ne (Get-Command candle.exe -ErrorAction SilentlyContinue)
    if ($haveWix) {
        Write-Output "== CPack WIX (MSI) =="
        $numeric = $version -replace '-.*$', ''
        & cpack -G WIX -C Release --config .\CPackConfig.cmake `
            -D "CPACK_PACKAGE_VERSION=$numeric" `
            -D "CPACK_PACKAGE_FILE_NAME=Palune-win-x64-msi"
        if ($LASTEXITCODE -ne 0) { throw "MSI 打包失败（退出码 $LASTEXITCODE）" }
    } else {
        Write-Warning "未找到 WiX candle.exe，跳过 MSI（仅生成 NSIS 安装包）"
    }
} finally {
    Pop-Location
}

# 收集并统一命名
$targets = @(
    @{ Filter = "Palune-$version-win-x64.exe"; Name = "Palune-$version-win-x64.exe" }
)
if ($haveWix) {
    $targets += @{ Filter = "Palune-win-x64-msi.msi"; Name = "Palune-$version-win-x64.msi" }
}
foreach ($t in $targets) {
    $file = Get-ChildItem -Path $BuildDir -Filter $t.Filter | Select-Object -First 1
    if (-not $file) { throw "未找到打包产物 $($t.Filter)" }
    Copy-Item $file.FullName (Join-Path $outFull $t.Name) -Force
    Write-Output ("{0} -> {1} ({2:N0} 字节)" -f $file.Name, $t.Name, $file.Length)
}

# 出包记录：把机器相关输入（MSVC Redist / Qt / 运行时依赖）的实际版本与哈希随包落盘，
# 后续能追溯到具体 redist 版本，而不是只写“随包部署了运行时”。
$stage = Join-Path $BuildDir "stage-record"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
& $CMakeExe --install $BuildDir --config Release --prefix $stage | Out-Null
if ($LASTEXITCODE -ne 0) { throw "安装暂存失败（退出码 $LASTEXITCODE）" }
$binDir = Join-Path $stage "bin"

# 打包门禁：安装树里缺任何一个运行时组件，装到用户机器上就是“Qt 平台插件初始化失败”
# 的英文弹框。这里直接失败，不产出安装包。
$requiredRuntime = @(
    "Palune.exe", "Qt6Core.dll", "Qt6Gui.dll", "platforms\qwindows.dll", "qt.conf",
    "libmpv-2.dll", "libsodium.dll", "vcruntime140.dll", "msvcp140.dll",
    "qml\QtQuick\qmldir"
)
$missing = @()
foreach ($rel in $requiredRuntime) {
    if (-not (Test-Path (Join-Path $binDir $rel))) { $missing += $rel }
}
if ($missing.Count -gt 0) {
    throw ("安装树缺少必要文件，拒绝出包：" + ($missing -join ", "))
}
Write-Output ("安装树自检通过：{0} 项运行时组件齐全" -f $requiredRuntime.Count)

$crtNames = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll",
              "msvcp140_1.dll", "msvcp140_2.dll", "concrt140.dll")
$redistRoots = @(Get-ChildItem "$env:ProgramFiles\Microsoft Visual Studio\*\*\VC\Redist\MSVC" `
                     -Directory -ErrorAction SilentlyContinue)
$redistCandidates = @()
foreach ($root in $redistRoots) {
    foreach ($verDir in (Get-ChildItem $root.FullName -Directory -ErrorAction SilentlyContinue)) {
        $x64 = Join-Path $verDir.FullName "x64"
        foreach ($crtDir in (Get-ChildItem $x64 -Directory -Filter "Microsoft.VC*.CRT" `
                                 -ErrorAction SilentlyContinue)) {
            $redistCandidates += Get-ChildItem $crtDir.FullName -Filter *.dll -File `
                -ErrorAction SilentlyContinue
        }
    }
}
$crtRecord = @()
foreach ($name in $crtNames) {
    $staged = Join-Path $binDir $name
    if (-not (Test-Path $staged)) { continue }
    $hash = (Get-FileHash $staged -Algorithm SHA256).Hash.ToLower()
    $source = $redistCandidates | Where-Object {
        $_.Name -eq $name -and (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower() -eq $hash
    } | Select-Object -First 1
    $crtRecord += [ordered]@{
        name            = $name
        bytes           = (Get-Item $staged).Length
        sha256          = $hash
        redistVersion   = if ($source -and $source.FullName -match '\\MSVC\\([^\\]+)\\') { $matches[1] } else { "" }
        sourcePath      = if ($source) { $source.FullName } else { "" }
    }
}

$qtCore = Join-Path $binDir "Qt6Core.dll"
$qtVersion = if (Test-Path $qtCore) { (Get-Item $qtCore).VersionInfo.FileVersion } else { "" }
$deps = @()
foreach ($dep in @("libmpv-2.dll", "libsodium.dll")) {
    $path = Join-Path $binDir $dep
    if (Test-Path $path) {
        $deps += [ordered]@{ name = $dep; bytes = (Get-Item $path).Length
                             sha256 = (Get-FileHash $path -Algorithm SHA256).Hash.ToLower() }
    }
}
$makensis = @("C:\Program Files (x86)\NSIS\makensis.exe",
              "C:\Program Files\NSIS\makensis.exe") | Where-Object { Test-Path $_ } |
            Select-Object -First 1
$nsisVersion = if ($makensis) { (& $makensis -VERSION 2>$null | Select-Object -First 1) } else { "" }

$cacheText = @(Get-Content (Join-Path $BuildDir "CMakeCache.txt") -ErrorAction SilentlyContinue)
$generator = ($cacheText | Where-Object { $_ -match '^CMAKE_GENERATOR:INTERNAL=' }) -replace
             '^CMAKE_GENERATOR:INTERNAL=', ''
$generatorInstance = ($cacheText | Where-Object { $_ -match '^CMAKE_GENERATOR_INSTANCE:INTERNAL=' }) -replace
                     '^CMAKE_GENERATOR_INSTANCE:INTERNAL=', ''
$redistVersions = @($crtRecord | ForEach-Object { $_["redistVersion"] } |
                    Where-Object { $_ } | Select-Object -Unique)
$compilerText = if ($generator) {
                    $parts = @($generator)
                    if ($generatorInstance) { $parts += "($generatorInstance)" }
                    if ($redistVersions) { $parts += "MSVC Redist $($redistVersions -join ', ')" }
                    $parts -join ' '
                } else { "" }

$record = [ordered]@{
    version        = $version
    channel        = "rc"
    generatedAt    = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ss")
    gitCommit      = (& git rev-parse HEAD 2>$null)
    buildType      = "Release"
    cmake          = (& $CMakeExe --version | Select-Object -First 1)
    generator      = $generator
    compiler       = $compilerText
    qtVersion      = $qtVersion
    toolchain      = [ordered]@{
        qtCoreSha256   = if (Test-Path $qtCore) { (Get-FileHash $qtCore -Algorithm SHA256).Hash.ToLower() } else { "" }
        nsisVersion    = $nsisVersion
    }
    msvcRedist     = $crtRecord
    runtimeDeps    = $deps
    artifacts      = @()
}
foreach ($t in $targets) {
    $pkg = Join-Path $outFull $t.Name
    $record.artifacts += [ordered]@{
        name   = $t.Name
        bytes  = (Get-Item $pkg).Length
        sha256 = (Get-FileHash $pkg -Algorithm SHA256).Hash.ToLower()
    }
}
$recordPath = Join-Path $outFull "build-record.json"
$record | ConvertTo-Json -Depth 8 | Set-Content $recordPath -Encoding UTF8
Write-Output "出包记录：$recordPath"

Write-Output "Windows 产物就绪：$outFull"
