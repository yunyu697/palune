$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing.Common

$cs = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class H {
    public delegate bool EnumProc(IntPtr h, IntPtr l);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct STARTUPINFO {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr CreateDesktopW(string name, IntPtr dev, IntPtr dm, int flags, uint access, IntPtr sa);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool CloseDesktop(IntPtr d);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool CreateProcessW(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit,
        uint flags, IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(IntPtr h, out uint code);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] public static extern bool TerminateProcess(IntPtr h, uint code);

    [DllImport("user32.dll")] public static extern bool EnumDesktopWindows(IntPtr d, EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern short VkKeyScanW(char ch);
    [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint code, uint mapType);

    public const uint DESKTOP_CREATEWINDOW = 0x0002, DESKTOP_ENUMERATE = 0x0040, DESKTOP_WRITEOBJECTS = 0x0080,
        DESKTOP_SWITCHDESKTOP = 0x0100, DESKTOP_READOBJECTS = 0x0001, GENERIC_ALL_DESKTOP = 0x10000000;
    public const uint PW_RENDERFULLCONTENT = 0x00000002;

    public static string Title(IntPtr h) { var sb = new StringBuilder(512); GetWindowTextW(h, sb, sb.Capacity); return sb.ToString(); }
    public static string Cls(IntPtr h) { var sb = new StringBuilder(256); GetClassNameW(h, sb, sb.Capacity); return sb.ToString(); }

    public class Win { public IntPtr H; public uint Pid; public string Title; public string Cls; public bool Visible; }

    public static List<Win> DesktopWindows(IntPtr desk) {
        var list = new List<Win>();
        EnumDesktopWindows(desk, (h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            list.Add(new Win { H = h, Pid = p, Title = Title(h), Cls = Cls(h), Visible = IsWindowVisible(h) });
            return true;
        }, IntPtr.Zero);
        return list;
    }

    public static bool Key(IntPtr h, int vk) {
        uint scan = MapVirtualKeyW((uint)vk, 0);
        IntPtr lp = (IntPtr)(1 | (scan << 16));
        bool a = PostMessage(h, 0x0100, (IntPtr)vk, lp);           // WM_KEYDOWN
        bool b = PostMessage(h, 0x0101, (IntPtr)vk, (IntPtr)(1u | (scan << 16) | (1u << 30) | (1u << 31))); // WM_KEYUP
        return a && b;
    }
}
'@
Add-Type -TypeDefinition $cs -ReferencedAssemblies @([System.Drawing.Bitmap].Assembly.Location, "System.Drawing.Common", "System.Runtime", "System.Collections", "System.Threading")

$root = 'D:\Users\Tyler Zhang\Documents\Trae-CN\temp_20261106_08\palune'
$exe = Join-Path $root 'build\Release\Palune.exe'
$media = Join-Path $root 'qa\runtime\media\resume-60s.mp4'
$outDir = Join-Path $root 'qa\runtime\probe-capture'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$deskName = 'PaluneProbeCapture'
$desk = [H]::CreateDesktopW($deskName, [IntPtr]::Zero, [IntPtr]::Zero, 0,
    0x000F01FF, [IntPtr]::Zero)
if ($desk -eq [IntPtr]::Zero) { throw "CreateDesktop 失败：$([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }

$si = New-Object H+STARTUPINFO
$si.cb = [Runtime.InteropServices.Marshal]::SizeOf([type][H+STARTUPINFO])
$si.lpDesktop = $deskName
$pi = New-Object H+PROCESS_INFORMATION
$cmd = "`"$exe`" --open `"$media`""
$ok = [H]::CreateProcessW($exe, $cmd, [IntPtr]::Zero, [IntPtr]::Zero, $false, 0x08000000, [IntPtr]::Zero, (Split-Path $exe), [ref]$si, [ref]$pi)
if (-not $ok) { throw "CreateProcess 失败：$([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
Write-Host ("已在隐藏桌面启动 pid={0}" -f $pi.dwProcessId)
Start-Sleep -Seconds 8

$wins = [H]::DesktopWindows($desk) | Where-Object { $_.Pid -eq $pi.dwProcessId -and $_.Title }
foreach ($w in $wins) { Write-Host ("窗口 hwnd={0} vis={1} cls={2} title='{3}'" -f $w.H, $w.Visible, $w.Cls, $w.Title) }

# 暂停，避免画面在比对过程中变化
$overlay = ($wins | Where-Object { $_.Title -like 'Palune*' -and $_.Title -notlike '*Video Surface*' } | Select-Object -First 1)
if ($overlay) { [void][H]::Key($overlay.H, 0x20); Start-Sleep -Seconds 2 }

function Save-PrintWindow([IntPtr]$h, [string]$path) {
    $r = New-Object H+RECT
    [void][H]::GetWindowRect($h, [ref]$r)
    $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($w -le 0 -or $ht -le 0) { return $null }
    $bmp = New-Object System.Drawing.Bitmap($w, $ht)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $res = [H]::PrintWindow($h, $hdc, [H]::PW_RENDERFULLCONTENT)
    $g.ReleaseHdc($hdc); $g.Dispose()
    if (-not $res) { $bmp.Dispose(); return $null }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    return $bmp
}

foreach ($w in $wins) {
    if (-not $w.Visible) { continue }
    $safe = ($w.Title -replace '[^\w\-]', '_')
    $p = Join-Path $outDir ("$safe.png")
    $bmp = Save-PrintWindow $w.H $p
    if (-not $bmp) { Write-Host ("  PrintWindow 失败：{0}" -f $w.Title); continue }
    # 统计颜色数与平均亮度：全黑/纯色说明隐藏桌面上的 PrintWindow 拿不到真实内容
    $colors = @{}
    $sum = 0.0; $n = 0
    for ($y = 0; $y -lt $bmp.Height; $y += 7) {
        for ($x = 0; $x -lt $bmp.Width; $x += 7) {
            $c = $bmp.GetPixel($x, $y)
            $sum += ($c.R + $c.G + $c.B) / 3.0; $n++
            $key = "{0},{1},{2}" -f $c.R, $c.G, $c.B
            if ($colors.Count -lt 5000) { $colors[$key] = 1 }
        }
    }
    Write-Host ("  {0}: {1}x{2} 平均亮度={3:N1} 采样色数={4} -> {5}" -f $w.Title, $bmp.Width, $bmp.Height, ($sum / [math]::Max($n, 1)), $colors.Count, $p)
    $bmp.Dispose()
}

[H]::TerminateProcess($pi.hProcess, 0) | Out-Null
[H]::CloseHandle($pi.hProcess) | Out-Null
[H]::CloseHandle($pi.hThread) | Out-Null
[H]::CloseDesktop($desk) | Out-Null
Write-Host '完成'
