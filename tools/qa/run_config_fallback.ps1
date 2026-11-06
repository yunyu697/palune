# M2 config fallback matrix: prepare config dirs, launch each in a hidden desktop,
# and aggregate the child reports into one evidence file.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Video,
    [string]$OutDir = "qa/runtime",
    [string]$Launcher = "tools/m0/run_hidden_selftest.ps1",
    [int]$TimeoutSec = 60
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outFull = (Resolve-Path $OutDir).Path
$exeFull = (Resolve-Path $Exe).Path
$videoFull = (Resolve-Path $Video).Path

$cases = @(
    @{ name = "no-settings";   content = $null;                                       expectTheme = "A"; expectReason = $false },
    @{ name = "no-ui";         content = "[window]`nwidth = 1180`nheight = 700`n";     expectTheme = "A"; expectReason = $false },
    @{ name = "invalid-theme"; content = "[ui]`ntheme = `"Z`"`n";                      expectTheme = "A"; expectReason = $true },
    @{ name = "corrupt-toml";  content = "[ui`ntheme = `"B`"`n";                       expectTheme = "A"; expectReason = $true },
    @{ name = "valid-b";       content = "[ui]`ntheme = `"B`"`n";                      expectTheme = "B"; expectReason = $false }
)

$results = @()
foreach ($case in $cases) {
    $caseDir = Join-Path $outFull ("config-" + $case.name)
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $caseDir
    New-Item -ItemType Directory -Force -Path $caseDir | Out-Null
    if ($null -ne $case.content) {
        $settingsPath = Join-Path $caseDir "settings.toml"
        [System.IO.File]::WriteAllText($settingsPath, $case.content, [System.Text.Encoding]::UTF8)
    }
    $reportPath = Join-Path $caseDir "report.json"
    $env:PALUNE_CONFIG_DIR = $caseDir
    & $Launcher -Exe $exeFull -Mode "m2-config-check" -Video $videoFull `
        -Report $reportPath -WorkingDir $caseDir -TimeoutSec $TimeoutSec
    $code = $LASTEXITCODE
    $report = if (Test-Path $reportPath) { Get-Content $reportPath -Raw | ConvertFrom-Json } else { $null }
    $themeOk = $null -ne $report -and $report.theme -eq $case.expectTheme
    $reasonOk = $true
    if ($case.expectReason) { $reasonOk = $null -ne $report -and $report.fallbackReason -ne "" }
    $pass = ($code -eq 0) -and $themeOk -and $reasonOk -and $null -ne $report -and
        $report.playerAvailable -and $report.played -and $report.settingsParseFailed -eq
        ($case.name -eq "corrupt-toml")
    $results += [ordered]@{
        case = $case.name
        settings = $case.content
        expectedTheme = $case.expectTheme
        exitCode = $code
        theme = if ($report) { $report.theme } else { "" }
        fallbackReason = if ($report) { $report.fallbackReason } else { "" }
        parseFailed = if ($report) { $report.settingsParseFailed } else { $null }
        windowReadyMs = if ($report) { $report.windowReadyMs } else { $null }
        played = if ($report) { $report.played } else { $false }
        verdict = if ($pass) { "PASS" } else { "FAIL" }
    }
}
Remove-Item Env:PALUNE_CONFIG_DIR -ErrorAction SilentlyContinue

$overall = if (($results | Where-Object { $_.verdict -ne "PASS" }).Count -eq 0) { "PASS" } else { "FAIL" }
$aggregate = [ordered]@{
    mode = "m2-config-fallback"
    exe = $exeFull
    cases = $results
    verdict = $overall
}
$json = $aggregate | ConvertTo-Json -Depth 6
$json | Set-Content -Path (Join-Path $outFull "m2-config-fallback.json") -Encoding UTF8
Write-Output $json
if ($overall -ne "PASS") { exit 2 }
