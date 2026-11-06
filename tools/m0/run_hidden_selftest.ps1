# 在独立 Windows 桌面上运行 Palune 自测/证据采集：不占前台、不弹窗、不抢焦点。
# 关键点：mpv 的 VO 线程不会继承 SetThreadDesktop，必须用 STARTUPINFO.lpDesktop
# 让整个子进程（含其所有线程）创建在独立桌面上。
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Report,
    [string]$Mode = "m0-selftest",
    [string]$Video = "",
    [string]$Media = "",
    [int]$SwitchCount = 0,
    [int]$StateSwitchCount = 0,
    [int]$SoakMinute = 0,
    [string]$WorkingDir = "",
    [int]$TimeoutSec = 240
)

$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class PaluneHiddenDesktop {
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

    [DllImport("kernel32.dll")]
    public static extern bool GetExitCodeProcess(IntPtr handle, out uint code);

    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
}
"@

$desktopName = "PaluneM0Test_" + (Get-Date -Format "HHmmss")
$desk = [PaluneHiddenDesktop]::CreateDesktopW($desktopName, [NullString]::Value, [IntPtr]::Zero, 0, 0x1FF, [IntPtr]::Zero)
if ($desk -eq [IntPtr]::Zero) {
    $err = [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error())
    throw "CreateDesktopW 失败：$($err.Message)"
}

$si = New-Object -TypeName 'PaluneHiddenDesktop+STARTUPINFO'
$si.cb = [Runtime.InteropServices.Marshal]::SizeOf([type][PaluneHiddenDesktop+STARTUPINFO])
$si.lpDesktop = $desktopName

$cmd = '"' + $Exe + '" --' + $Mode + ' --hidden-desktop --report "' + $Report + '"'
if ($Video) { $cmd += ' --video "' + $Video + '"' }
if ($Media) { $cmd += ' --media "' + $Media + '"' }
if ($SwitchCount -gt 0) { $cmd += ' --switch-count ' + $SwitchCount }
if ($StateSwitchCount -gt 0) { $cmd += ' --state-switch-count ' + $StateSwitchCount }
if ($SoakMinute -gt 0) { $cmd += ' --soak-minute ' + $SoakMinute }

$pi = New-Object -TypeName 'PaluneHiddenDesktop+PROCESS_INFORMATION'
$ok = [PaluneHiddenDesktop]::CreateProcessW($Exe, $cmd, [IntPtr]::Zero, [IntPtr]::Zero,
    $false, 0, [IntPtr]::Zero, $WorkingDir, [ref]$si, [ref]$pi)
if (-not $ok) {
    [PaluneHiddenDesktop]::CloseDesktop($desk)
    $err = [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error())
    throw "CreateProcessW 失败：$($err.Message)"
}

Write-Output "已在独立桌面 $desktopName 启动 M0 自测（pid=$($pi.dwProcessId)）"
$wait = [PaluneHiddenDesktop]::WaitForSingleObject($pi.hProcess, $TimeoutSec * 1000)
$code = [uint32]0
[PaluneHiddenDesktop]::GetExitCodeProcess($pi.hProcess, [ref]$code) | Out-Null
[PaluneHiddenDesktop]::CloseHandle($pi.hThread) | Out-Null
[PaluneHiddenDesktop]::CloseHandle($pi.hProcess) | Out-Null
[PaluneHiddenDesktop]::CloseDesktop($desk) | Out-Null

if ($wait -ne 0) { throw "自测超时（${TimeoutSec}s）" }
Write-Output "自测进程退出码：$code"
exit $code
