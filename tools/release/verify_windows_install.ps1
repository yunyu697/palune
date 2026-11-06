# Verify a Windows installer end to end: install -> files/shortcuts -> launch -> uninstall.
# Evidence is written as JSON so the release pipeline can upload it.
param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [string]$OutDir = "qa/runtime",
    [string]$InstallDir = "$env:SystemDrive\PaluneVerify",
    [string]$Launcher = "tools/m0/run_hidden_selftest.ps1",
    [int]$TimeoutSec = 60
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outFull = (Resolve-Path $OutDir).Path
$installerFull = (Resolve-Path $Installer).Path
$commonStart = [Environment]::GetFolderPath("CommonStartMenu")
$commonDesktop = [Environment]::GetFolderPath("CommonDesktopDirectory")
$startMenuDir = Join-Path $commonStart "Programs\Palune"
$startExe = Join-Path $startMenuDir "Palune.lnk"
$startUninstall = Join-Path $startMenuDir "Uninstall Palune.lnk"
$desktopLink = Join-Path $commonDesktop "Palune.lnk"
$uninstaller = Join-Path $InstallDir "uninstall.exe"

function Wait-Gone([string]$Path, [int]$Seconds = 60) {
    for ($i = 0; $i -lt $Seconds; $i++) {
        if (-not (Test-Path $Path)) { return $true }
        Start-Sleep -Seconds 1
    }
    return -not (Test-Path $Path)
}

function Link-Target([string]$Link) {
    if (-not (Test-Path $Link)) { return "" }
    $shell = New-Object -ComObject WScript.Shell
    return $shell.CreateShortcut($Link).TargetPath
}

$evidence = [ordered]@{
    installer = $installerFull
    installDir = $InstallDir
    expectedShortcuts = @($startExe, $startUninstall, $desktopLink)
    installExitCode = $null
    files = [ordered]@{}
    shortcuts = [ordered]@{}
    uninstallEntry = [ordered]@{}
    launch = [ordered]@{}
    uninstall = [ordered]@{}
    verdict = "PENDING"
}

# Clean any previous attempt so results are deterministic.
if (Test-Path $uninstaller) {
    Start-Process -FilePath $uninstaller -ArgumentList "/S" -Wait
    Wait-Gone $InstallDir 60 | Out-Null
}
Remove-Item -Force -ErrorAction SilentlyContinue $startExe, $startUninstall, $desktopLink
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $startMenuDir
if (Test-Path $InstallDir) { Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $InstallDir }

Write-Output "Installing silently to $InstallDir ..."
$proc = Start-Process -FilePath $installerFull -ArgumentList "/S", "/D=$InstallDir" -Wait -PassThru
$evidence.installExitCode = $proc.ExitCode
if ($proc.ExitCode -ne 0) { throw "installer exit code $($proc.ExitCode)" }
Start-Sleep -Seconds 2

$required = @(
    "bin\Palune.exe",
    "bin\Qt6Core.dll",
    "bin\Qt6Gui.dll",
    "bin\Qt6Qml.dll",
    "bin\Qt6Quick.dll",
    "bin\Qt6QuickControls2.dll",
    "bin\platforms\qwindows.dll",
    "bin\vcruntime140.dll",
    "bin\msvcp140.dll",
    "bin\libmpv-2.dll",
    "bin\libsodium.dll",
    "bin\NOTICE.md",
    "uninstall.exe"
)
foreach ($rel in $required) {
    $full = Join-Path $InstallDir $rel
    $evidence.files[$rel] = (Test-Path $full)
}
$qmlDir = Join-Path $InstallDir "bin\qml"
$evidence.files["bin\qml"] = (Test-Path $qmlDir) -and @(Get-ChildItem $qmlDir -Recurse -File -ErrorAction SilentlyContinue).Count -gt 0

$evidence.shortcuts["startMenuExe"] = Link-Target $startExe
$evidence.shortcuts["startMenuUninstall"] = Link-Target $startUninstall
$evidence.shortcuts["desktop"] = Link-Target $desktopLink
# 「所有程序」根目录不应出现 Palune 相关链接（模板默认项容易漏到这里）。
$startMenuRoot = Join-Path $commonStart "Programs"
$evidence.shortcuts["startMenuRootStray"] = @(
    Get-ChildItem $startMenuRoot -Filter "*.lnk" -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "*Palune*" } | ForEach-Object { $_.FullName }
)

$uninstallKeys = @(
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Palune",
    "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Palune",
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Palune"
)
$evidence.uninstallEntry["uninstallerFile"] = Test-Path $uninstaller
$evidence.uninstallEntry["registryKeys"] = @($uninstallKeys | Where-Object { Test-Path $_ })

# Launch the installed binary through a hidden desktop so no window pops up.
$report = Join-Path $outFull "install-launch.json"
Remove-Item -Force -ErrorAction SilentlyContinue $report
Write-Output "Launching installed binary ..."
& $Launcher -Exe (Join-Path $InstallDir "bin\Palune.exe") -Mode "m2-config-check" `
    -Report $report -WorkingDir $InstallDir -TimeoutSec $TimeoutSec
$launchCode = $LASTEXITCODE
$launchReport = if (Test-Path $report) { Get-Content $report -Raw | ConvertFrom-Json } else { $null }
$evidence.launch["exitCode"] = $launchCode
$evidence.launch["report"] = $launchReport
$evidence.launch["ok"] = ($launchCode -eq 0) -and $null -ne $launchReport -and
    $launchReport.verdict -eq "PASS"

# Uninstall and confirm shortcuts + files disappear.
Write-Output "Uninstalling silently ..."
Start-Process -FilePath $uninstaller -ArgumentList "/S" -Wait
$installGone = Wait-Gone $InstallDir 90
Start-Sleep -Seconds 2
$evidence.uninstall["installDirGone"] = $installGone
$evidence.uninstall["startMenuGone"] = -not (Test-Path $startMenuDir)
$evidence.uninstall["desktopGone"] = -not (Test-Path $desktopLink)

$filesOk = -not ($evidence.files.Values | Where-Object { -not $_ })
$startOk = ($evidence.shortcuts.startMenuExe -like "*\bin\Palune.exe")
$desktopOk = ($evidence.shortcuts.desktop -like "*\bin\Palune.exe")
$uninstallOk = $evidence.uninstallEntry.uninstallerFile -and
    ($evidence.uninstallEntry.registryKeys.Count -ge 1)
$uninstallClean = $evidence.uninstall.installDirGone -and $evidence.uninstall.startMenuGone -and
    $evidence.uninstall.desktopGone

$noStray = @($evidence.shortcuts.startMenuRootStray).Count -eq 0
$pass = ($evidence.installExitCode -eq 0) -and $filesOk -and $startOk -and $desktopOk -and $noStray -and
    $uninstallOk -and $evidence.launch.ok -and $uninstallClean
$evidence.verdict = if ($pass) { "PASS" } else { "FAIL" }

$json = $evidence | ConvertTo-Json -Depth 6
$json | Set-Content -Path (Join-Path $outFull "windows-install-evidence.json") -Encoding UTF8
Write-Output $json
if (-not $pass) { exit 2 }
