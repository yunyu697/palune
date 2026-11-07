# M2：强杀续播验证 —— 10 轮「播放 → 硬杀 → 重启续播」。
# 全程在独立 Windows 桌面运行，不占前台、不弹窗。
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Media,
    [string]$OutFile = "docs/evidence/m2-resume-kill.json",
    [string]$ConfigDir = "qa/runtime/config-resume",
    [int]$Rounds = 10,
    [int]$PlaySeconds = 12
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutFile) | Out-Null
$env:PALUNE_CONFIG_DIR = (Resolve-Path -LiteralPath ".").Path + "\" + $ConfigDir
Remove-Item -Recurse -Force $ConfigDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $ConfigDir | Out-Null

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class PaluneKillDesktop {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct STARTUPINFO {
        public int cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute;
        public int dwFlags;
        public short wShowWindow;
        public short cbReserved2;
        public IntPtr lpReserved2;
        public IntPtr hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION {
        public IntPtr hProcess, hThread;
        public int dwProcessId, dwThreadId;
    }
    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateDesktopW(string name, string device, IntPtr devmode,
        int flags, uint access, IntPtr sa);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool CloseDesktop(IntPtr desk);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool CreateProcessW(string app, string cmd, IntPtr pa, IntPtr ta,
        bool inherit, uint flags, IntPtr env, string cwd, ref STARTUPINFO si,
        out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll")]
    public static extern uint WaitForSingleObject(IntPtr handle, uint ms);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool TerminateProcess(IntPtr process, uint code);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
}
"@

$desktopName = "PaluneResume_" + (Get-Date -Format "HHmmss")
$desk = [PaluneKillDesktop]::CreateDesktopW($desktopName, [NullString]::Value, [IntPtr]::Zero, 0, 0x1FF, [IntPtr]::Zero)
if ($desk -eq [IntPtr]::Zero) { throw "CreateDesktopW 失败" }

function Start-Hidden([string]$arguments, [string]$cwd) {
    $si = New-Object -TypeName 'PaluneKillDesktop+STARTUPINFO'
    $si.cb = [Runtime.InteropServices.Marshal]::SizeOf([type][PaluneKillDesktop+STARTUPINFO])
    $si.lpDesktop = $desktopName
    $pi = New-Object -TypeName 'PaluneKillDesktop+PROCESS_INFORMATION'
    $ok = [PaluneKillDesktop]::CreateProcessW($Exe, $arguments, [IntPtr]::Zero, [IntPtr]::Zero,
        $false, 0, [IntPtr]::Zero, $cwd, [ref]$si, [ref]$pi)
    if (-not $ok) { throw "CreateProcessW 失败：$([ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error()).Message)" }
    return $pi
}

$root = (Resolve-Path -LiteralPath ".").Path
$mediaPath = (Resolve-Path -LiteralPath $Media).Path
$results = @()
$allPass = $true

for ($i = 1; $i -le $Rounds; $i++) {
    Write-Output "第 $i 轮：播放 $PlaySeconds 秒后强杀"
    $cmd = '"' + $Exe + '" --open "' + $mediaPath + '" --hidden-desktop'
    $pi = Start-Hidden $cmd $root
    Start-Sleep -Seconds $PlaySeconds
    [PaluneKillDesktop]::TerminateProcess($pi.hProcess, 1) | Out-Null
    [PaluneKillDesktop]::WaitForSingleObject($pi.hProcess, 5000) | Out-Null
    [PaluneKillDesktop]::CloseHandle($pi.hThread) | Out-Null
    [PaluneKillDesktop]::CloseHandle($pi.hProcess) | Out-Null
    Start-Sleep -Seconds 1

    $probeReport = "$ConfigDir/probe-$i.json"
    Remove-Item -Force $probeReport -ErrorAction SilentlyContinue
    $probeCmd = '"' + $Exe + '" --open "' + $mediaPath + '" --resume-probe --hidden-desktop --report "' + (Join-Path $root $probeReport) + '"'
    $pi = Start-Hidden $probeCmd $root
    [PaluneKillDesktop]::WaitForSingleObject($pi.hProcess, 30000) | Out-Null
    [PaluneKillDesktop]::CloseHandle($pi.hThread) | Out-Null
    [PaluneKillDesktop]::CloseHandle($pi.hProcess) | Out-Null

    if (-not (Test-Path $probeReport)) {
        $results += [pscustomobject]@{ round = $i; loaded = $false; saved = 0; restored = 0; delta = 999; pass = $false }
        $allPass = $false
        continue
    }
    $probe = Get-Content $probeReport -Raw | ConvertFrom-Json
    $pass = $probe.loaded -and ($probe.deltaSeconds -le 1.0)
    $allPass = $allPass -and $pass
    $results += [pscustomobject]@{
        round = $i; loaded = $probe.loaded; saved = [math]::Round($probe.savedPosition, 2);
        restored = [math]::Round($probe.restoredPosition, 2);
        delta = [math]::Round($probe.deltaSeconds, 3); pass = $pass
    }
    Write-Output ("  续播 saved={0:N2}s restored={1:N2}s delta={2:N3}s {3}" -f $probe.savedPosition, $probe.restoredPosition, $probe.deltaSeconds, $(if ($pass) { "PASS" } else { "FAIL" }))
}

[PaluneKillDesktop]::CloseDesktop($desk) | Out-Null

$report = [pscustomobject]@{
    mode = "m2-resume-kill"
    rounds = $Rounds
    toleranceSeconds = 1.0
    media = $mediaPath
    items = $results
    verdict = $(if ($allPass) { "PASS" } else { "FAIL" })
    generatedAt = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssK")
}
$report | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $OutFile
Write-Output "结果写入 $OutFile，verdict=$($report.verdict)"
