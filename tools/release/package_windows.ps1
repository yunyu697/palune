# Windows 打包：NSIS 安装器（.exe）+ MSI，产物统一收进 dist/。
param(
    [string]$BuildDir = "build",
    [string]$OutDir = "dist"
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

Write-Output "Windows 产物就绪：$outFull"
