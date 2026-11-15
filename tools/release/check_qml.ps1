# QML 静态门禁：用 qmllint 扫全部界面文件，只对“结构性错误”失败。
# 背景：qml/SettingsPanel.qml 曾把 font.family 写在 Rectangle 上，编译能过，
# 但运行到创建窗口时整个 Main.qml 起不来——这类错误必须在出包前拦住。
# “unqualified” 等风格类警告不计入，避免把现有代码一次性全部染红。
param(
    [string]$QmlDir = "qml",
    [string]$OutFile = "",
    [string]$Qmllint = ""
)

$ErrorActionPreference = "Stop"

if (-not $Qmllint) {
    $cmd = Get-Command qmllint -ErrorAction SilentlyContinue
    if ($cmd) {
        $Qmllint = $cmd.Source
    }
}
if (-not $Qmllint -or -not (Test-Path $Qmllint)) {
    $candidates = @("C:\Qt\6.8.3\msvc2022_64\bin\qmllint.exe")
    if ($env:QT_ROOT) { $candidates += (Join-Path $env:QT_ROOT "bin\qmllint.exe") }
    $Qmllint = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
}
if (-not $Qmllint) {
    throw "找不到 qmllint；请把 Qt 的 bin 目录加入 PATH 或用 -Qmllint 指定"
}

$files = @(Get-ChildItem -Path $QmlDir -Recurse -Filter *.qml -File | ForEach-Object { $_.FullName })
if ($files.Count -eq 0) { throw "在 $QmlDir 下没有找到 QML 文件" }

# qmllint 把警告写到 stderr；在 Stop 模式下第一行会被当成终止错误，这里临时放宽。
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $Qmllint @files 2>&1 | ForEach-Object { "$_" }
$ErrorActionPreference = $previousPreference
if ($OutFile) {
    $output | Set-Content -Path $OutFile -Encoding UTF8
}

$pattern = '\[(missing-property|unresolved-type|syntax|compiler)\]'
$problems = @($output | Select-String -Pattern $pattern)
if ($problems.Count -gt 0) {
    $problems | ForEach-Object { Write-Output $_.Line }
    Write-Output "qmllint：$($files.Count) 个文件，发现 $($problems.Count) 条结构性错误"
    exit 2
}

Write-Output "qmllint：$($files.Count) 个文件，未发现结构性错误"
exit 0
