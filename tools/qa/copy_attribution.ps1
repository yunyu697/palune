#requires -Version 5.1
<#
.SYNOPSIS
  副本归属定案：把机器上每个 Palune.exe 逐个真启动，抓它自己的窗口/对话框原文。

  为什么不用「肉眼双击看弹窗」：
    - 载入器失败框（STATUS_DLL_NOT_FOUND）由 csrss.exe 持有，会抢前台、留在桌面上，
      逐个点一遍会污染用户桌面。
    - 这里每个副本都在**独立隐藏桌面**（CreateDesktop）上启动，弹窗只出现在那个桌面上，
      前台不受影响；枚举也走 EnumDesktopWindows，拿到的就是该副本自己的顶层窗口。

  对每个副本记录：完整路径 / 文件版本 / 旁边有无 Qt6Core|Qt6Quick|Qt6Widgets / platforms\qwindows.dll /
  qt.conf / 进程退出码 / 该副本弹出的窗口标题 / #32770 对话框正文原文。
#>
param(
    [string]$Out = ""
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Out) { $Out = Join-Path $root 'qa\evidence\copies' }
New-Item -ItemType Directory -Force -Path $Out | Out-Null

$cs = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class Desk
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct STARTUPINFO
    {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr CreateDesktopW(string name, IntPtr dev, IntPtr dm, int flags, uint access, IntPtr sa);
    [DllImport("user32.dll")] public static extern bool CloseDesktop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool EnumDesktopWindows(IntPtr h, EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr w, StringBuilder l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
    public delegate bool EnumProc(IntPtr h, IntPtr l);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool CreateProcessW(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit,
        uint flags, IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(IntPtr h, out uint code);
    [DllImport("kernel32.dll")] public static extern bool TerminateProcess(IntPtr h, uint code);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);

    public static string DesktopName = "PaluneQAVerify";
    public static IntPtr Desktop;

    public static void OpenDesktop()
    {
        Desktop = CreateDesktopW(DesktopName, IntPtr.Zero, IntPtr.Zero, 0, 0x01FF /*GENERIC_ALL*/, IntPtr.Zero);
        if (Desktop == IntPtr.Zero) throw new Exception("CreateDesktop 失败，err=" + Marshal.GetLastWin32Error());
    }
    public static void Close() { if (Desktop != IntPtr.Zero) { CloseDesktop(Desktop); Desktop = IntPtr.Zero; } }

    // 在该隐藏桌面上启动副本，返回窗口清单与退出码。
    public static string RunCopy(string exe)
    {
        var si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
        si.lpDesktop = DesktopName;
        si.dwFlags = 0x00000001;   // STARTF_USESHOWWINDOW
        si.wShowWindow = 1;        // SW_SHOWNORMAL
        PROCESS_INFORMATION pi;
        string dir = System.IO.Path.GetDirectoryName(exe);
        if (!CreateProcessW(exe, "\"" + exe + "\"", IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, dir, ref si, out pi))
            return "CREATE_FAIL|err=" + Marshal.GetLastWin32Error();

        uint wait = WaitForSingleObject(pi.hProcess, 6000);
        bool exited = (wait == 0);
        uint code = 0;
        GetExitCodeProcess(pi.hProcess, out code);

        var sb = new StringBuilder();
        sb.Append("EXITED=").Append(exited ? "1" : "0").Append("|CODE=0x").Append(code.ToString("X8")).Append("|WIN=");
        var titles = new List<string>();
        EnumDesktopWindows(Desktop, (h, l) =>
        {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if ((int)pid != pi.dwProcessId) return true;
            var t = new StringBuilder(512); GetWindowTextW(h, t, t.Capacity);
            var c = new StringBuilder(128); GetClassNameW(h, c, c.Capacity);
            if (t.Length == 0) return true;
            titles.Add(c + " :: " + t);
            if (c.ToString() == "#32770")
            {
                EnumChildWindows(h, (ch, ll) =>
                {
                    var body = new StringBuilder(2048);
                    SendMessageW(ch, 0x000D, IntPtr.Zero, body);
                    if (body.Length > 0) titles.Add("    DIALOG-BODY: " + body.ToString().Trim());
                    return true;
                }, IntPtr.Zero);
                PostMessageW(h, 0x0010 /*WM_CLOSE*/, IntPtr.Zero, IntPtr.Zero);
            }
            return true;
        }, IntPtr.Zero);
        sb.Append(titles.Count == 0 ? "(none)" : string.Join(" ;; ", titles.ToArray()));

        if (!exited)
        {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 3000);
        }
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return sb.ToString();
    }
}
'@
Add-Type -TypeDefinition $cs | Out-Null

# ---------------------------------------------------------------- 收集所有副本
$roots = @(
    (Join-Path $root 'build'),
    (Join-Path $root 'build-m2'),
    (Join-Path $root 'build-m2b'),
    (Join-Path $root 'dist'),
    (Join-Path $root 'qa'),
    (Join-Path $root '_scratch'),
    (Join-Path (Split-Path -Parent $root) '_CPack_Packages'),
    $env:LOCALAPPDATA + '\Programs',
    $env:LOCALAPPDATA + '\Temp',
    (Join-Path $env:USERPROFILE 'Downloads'),
    'C:\PaluneVerify', 'C:\PaluneVerify2', 'C:\Temp\palune-rc-install',
    'C:\Program Files\Palune', 'C:\Program Files (x86)\Palune'
)
$exes = @()
foreach ($r in $roots) {
    if ($r -and (Test-Path $r)) {
        $exes += Get-ChildItem -Path $r -Filter 'Palune.exe' -Recurse -File -ErrorAction SilentlyContinue
    }
}
# MuiCache 里记录过的历史路径（即使目录已删也列出来）
$mui = 'HKCU:\Software\Classes\Local Settings\Software\Microsoft\Windows\Shell\MuiCache'
$muiPaths = @()
if (Test-Path $mui) {
    $muiPaths = (Get-Item $mui).GetValueNames() | Where-Object { $_ -like '*Palune.exe*' } |
        ForEach-Object { $_.Substring(0, $_.Length - '.FriendlyAppName'.Length) } | Select-Object -Unique
}
$exes = $exes | Sort-Object FullName -Unique

[Desk]::OpenDesktop()

$rows = @()
foreach ($e in $exes) {
    $dir = Split-Path -Parent $e.FullName
    $has = { param($n) Test-Path (Join-Path $dir $n) }
    $ver = ''
    try { $ver = (Get-Item $e.FullName).VersionInfo.FileVersion } catch {}
    $row = [ordered]@{
        path        = $e.FullName
        version     = $ver
        size        = $e.Length
        modified    = $e.LastWriteTime.ToString('yyyy-MM-dd HH:mm')
        qtCore      = & $has 'Qt6Core.dll'
        qtQuick     = & $has 'Qt6Quick.dll'
        qtWidgets   = & $has 'Qt6Widgets.dll'
        qwindows    = & $has 'platforms\qwindows.dll'
        qtConf      = & $has 'qt.conf'
        run         = ''
    }
    $row.run = [Desk]::RunCopy($e.FullName)
    Write-Host ("== {0}" -f $e.FullName)
    Write-Host ("   Core={0} Quick={1} Widgets={2} qwindows={3} qt.conf={4}" -f $row.qtCore, $row.qtQuick, $row.qtWidgets, $row.qwindows, $row.qtConf)
    Write-Host ("   {0}" -f $row.run)
    $rows += [pscustomobject]$row
}
[Desk]::Close()

$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$result = [pscustomobject]@{
    generatedAt  = $stamp
    copies       = $rows
    muiCachePath = $muiPaths
}
$result | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $Out 'result.json')

$md = @()
$md += "# Palune.exe 副本逐个真启动取证"
$md += ""
$md += "- 生成时间：$stamp"
$md += "- 方式：每个副本在独立隐藏桌面（``$([Desk]::DesktopName)``）上按「工作目录 = exe 所在目录」启动，等 6s，"
$md += "  枚举该 pid 的顶层窗口，读 ``#32770`` 对话框正文原文，然后结束进程。前台不受影响。"
$md += ""
$md += "| # | 副本 | 版本 | Qt6Core | Qt6Quick | Qt6Widgets | platforms\qwindows.dll | qt.conf | 启动结果 |"
$md += "| --- | --- | --- | --- | --- | --- | --- | --- | --- |"
$i = 0
foreach ($r in $rows) {
    $i++
    $run = $r.run
    if ($run.Length -gt 260) { $run = $run.Substring(0, 260) + '…' }
    $md += ("| {0} | ``{1}`` | {2} | {3} | {4} | {5} | {6} | {7} | {8} |" -f $i, $r.path, $r.version,
        $r.qtCore, $r.qtQuick, $r.qtWidgets, $r.qwindows, $r.qtConf, $run.Replace('|', '/'))
}
$md += ""
$md += "## MuiCache 里出现过的历史路径"
$md += ""
foreach ($p in $muiPaths) {
    $exists = if (Test-Path $p) { '存在' } else { '目录已不存在' }
    $md += "- ``$p``（$exists）"
}
$md | Set-Content -Encoding UTF8 (Join-Path $Out 'report.md')
Write-Host ""
Write-Host "结果：$(Join-Path $Out 'result.json')"
