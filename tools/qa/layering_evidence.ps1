#requires -Version 5.1
<#
.SYNOPSIS
  覆盖/还原类渲染缺陷的逐格取证（独立桌面版）。

约束（来自用户，硬性）：
  - 所有涉及窗口的验证跑在独立桌面（CreateDesktop + STARTUPINFO.lpDesktop），
    不落在用户交互桌面，不抢焦点、不弹窗到用户面前。
  - 禁止真实 Alt+Tab、禁止任何真实前台按键序列；按键用 PostMessage 定向到目标 hwnd。
  - 隐藏桌面上做不出来的行为（真实 Alt+Tab 的 z 序重排、屏幕合成层残留）标注
    「未在真实前台验证」，不回落到用户桌面补数。

判据分两层，报告里分开写：
  1) 结构判据：video 窗是否仍紧贴 overlay 正下方（GetWindow(video, GW_HWNDPREV) == overlay）、
     两窗是否仍可见、矩形是否仍一致。
  2) 渲染连续性：PrintWindow(PW_RENDERFULLCONTENT) 直接取窗口自身像素（不经屏幕合成），
     扰动前/恢复后差异 >15% 记为异常。注意：它只能证明「窗口自己还在正常画」，
     不能证明「屏幕上没有遮挡残留」——后者需要真实前台，本脚本不测。
#>
param(
    [string]$Exe = "",
    [string]$Media = "",
    [string]$Out = "",
    [int]$Repeats = 3,
    # 对照组用：不发 Space，让动态样片继续播放，用来判断「还原后全黑」是不是
    # 「静态画面在交换链重建后没有新帧可画」造成的取样假象。
    [switch]$NoPause,
    [switch]$KeepRunning
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Exe) { $Exe = Join-Path $root 'build\Release\Palune.exe' }
if (-not $Out) { $Out = Join-Path $root 'qa\evidence\layering' }
if (-not (Test-Path $Exe)) { throw "找不到 Palune.exe：$Exe" }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Add-Type -AssemblyName System.Drawing.Common

# 像素判据的前提是「扰动前后画面可比」。动态样片每一帧都在变，两次取样天然有差异，
# 判据就不成立（旧口径用 resume-60s.mp4 时 0.0820 的「差异」全是画面运动）。
# 这里生成一张确定的静态图交给 mpv（MpvClient 设了 keep-open=yes，停在最后一帧），
# 前后取样才真正可比。--Media 仍可传入别的样片，但动态画面会被下面的门控判为不可用。
if (-not $Media) {
    $Media = Join-Path $Out 'still-fixture.png'
    $bmp = New-Object System.Drawing.Bitmap(1280, 720)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb(255, 16, 18, 24))
    $bars = @(
        @{ C = @(235, 235, 235); X = 0 },   @{ C = @(235, 215, 40); X = 160 },
        @{ C = @(40, 200, 220);  X = 320 }, @{ C = @(40, 200, 70);  X = 480 },
        @{ C = @(210, 50, 200);  X = 640 }, @{ C = @(220, 50, 50);  X = 800 },
        @{ C = @(50, 80, 220);   X = 960 }, @{ C = @(24, 24, 28);   X = 1120 })
    foreach ($b in $bars) {
        $c = [System.Drawing.Color]::FromArgb(255, $b.C[0], $b.C[1], $b.C[2])
        $brush = New-Object System.Drawing.SolidBrush($c)
        $g.FillRectangle($brush, $b.X, 0, 160, 520)
        $brush.Dispose()
    }
    for ($x = 0; $x -lt 1280; $x++) {
        $v = [int](255 * $x / 1279)
        $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, $v, 255 - $v, 128))
        $g.DrawLine($pen, $x, 520, $x, 719)
        $pen.Dispose()
    }
    $g.Dispose()
    $bmp.Save($Media, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host ("已生成静态测试图：{0}" -f $Media)
}
if (-not (Test-Path $Media)) { throw "找不到测试样片：$Media" }

$cs = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

public static class QD
{
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
    [StructLayout(LayoutKind.Sequential)]
    public struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam; public IntPtr lParam; public uint time; public int x, y; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct WNDCLASS { public uint style; public WndProc lpfnWndProc; public int cbClsExtra, cbWndExtra;
        public IntPtr hInstance, hIcon, hCursor, hbrBackground; public string lpszMenuName, lpszClassName; }
    [StructLayout(LayoutKind.Sequential)]
    public struct PAINTSTRUCT { public IntPtr hdc; public bool fErase; public RECT rcPaint; public bool fRestore, fIncUpdate;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)] public byte[] rgbReserved; }
    public delegate IntPtr WndProc(IntPtr h, uint m, IntPtr w, IntPtr l);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr CreateDesktopW(string name, IntPtr dev, IntPtr dm, int flags, uint access, IntPtr sa);
    [DllImport("user32.dll")] public static extern bool CloseDesktop(IntPtr d);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool CreateProcessW(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit,
        uint flags, IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll")] public static extern bool TerminateProcess(IntPtr h, uint code);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);

    [DllImport("user32.dll")] public static extern bool EnumDesktopWindows(IntPtr d, EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern bool SetThreadDesktop(IntPtr desk);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr CreateWindowExW(uint ex, string cls, string name,
        uint style, int x, int y, int cx, int cy, IntPtr parent, IntPtr menu, IntPtr inst, IntPtr param);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern ushort RegisterClassW(ref WNDCLASS wc);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr DefWindowProcW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern int GetMessageW(out MSG m, IntPtr h, uint min, uint max);
    [DllImport("user32.dll")] public static extern bool TranslateMessage(ref MSG m);
    [DllImport("user32.dll")] public static extern IntPtr DispatchMessageW(ref MSG m);
    [DllImport("user32.dll")] public static extern IntPtr BeginPaint(IntPtr h, out PAINTSTRUCT ps);
    [DllImport("user32.dll")] public static extern bool EndPaint(IntPtr h, ref PAINTSTRUCT ps);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateSolidBrush(int color);
    [DllImport("user32.dll")] public static extern int FillRect(IntPtr hdc, ref RECT r, IntPtr brush);

    public const uint GW_HWNDPREV = 3;
    public const uint PW_RENDERFULLCONTENT = 2;
    public const int SW_MINIMIZE = 6, SW_SHOWNOACTIVATE = 4;

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

    // 定向投递按键：不依赖前台、不激活窗口
    public static bool Key(IntPtr h, int vk) {
        uint scan = MapVirtualKeyW((uint)vk, 0);
        bool a = PostMessage(h, 0x0100, (IntPtr)vk, (IntPtr)(1 | (scan << 16)));
        bool b = PostMessage(h, 0x0101, (IntPtr)vk, (IntPtr)(1u | (scan << 16) | (1u << 30) | (1u << 31)));
        return a && b;
    }

    // ---- 遮挡窗：必须建在同一個隐藏桌面上，所以先 SetThreadDesktop 再建窗 ----
    public static IntPtr Cover = IntPtr.Zero;
    static Thread coverThread;
    static ManualResetEvent coverReady = new ManualResetEvent(false);

    public static void StartCover(IntPtr desk, int w, int h) {
        coverThread = new Thread(() => {
            SetThreadDesktop(desk);
            IntPtr brush = CreateSolidBrush(0x00FF00FF);
            var wc = new WNDCLASS();
            wc.lpfnWndProc = (win, m, a, b) => {
                if (m == 0x000F) {
                    PAINTSTRUCT ps; IntPtr hdc = BeginPaint(win, out ps);
                    RECT r; GetClientRect(win, out r);
                    FillRect(hdc, ref r, brush);
                    EndPaint(win, ref ps);
                    return IntPtr.Zero;
                }
                if (m == 0x0001) return IntPtr.Zero;
                return DefWindowProcW(win, m, a, b);
            };
            wc.lpszClassName = "PaluneQaCoverHidden";
            wc.hbrBackground = brush;
            RegisterClassW(ref wc);
            Cover = CreateWindowExW(0, "PaluneQaCoverHidden", "PaluneQA Cover",
                0x80000000 /*WS_POPUP*/ | 0x10000000 /*WS_VISIBLE*/,
                0, 0, w, h, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
            coverReady.Set();
            MSG msg;
            while (GetMessageW(out msg, IntPtr.Zero, 0, 0) > 0) { TranslateMessage(ref msg); DispatchMessageW(ref msg); }
        });
        coverThread.IsBackground = true;
        coverThread.SetApartmentState(ApartmentState.STA);
        coverThread.Start();
        coverReady.WaitOne(10000);
    }

    public static void CoverShow(IntPtr overlay, RECT r) {
        SetWindowPos(Cover, overlay, r.Left, r.Top, r.Right - r.Left, r.Bottom - r.Top,
            0x0040 /*SHOWWINDOW*/ | 0x0010 /*NOACTIVATE*/);
    }
    public static void CoverHide() { ShowWindow(Cover, SW_SHOWNOACTIVATE); SetWindowPos(Cover, IntPtr.Zero, -20000, -20000, 200, 200, 0x0004 | 0x0010); }
}
'@
Add-Type -TypeDefinition $cs -ReferencedAssemblies @([System.Drawing.Bitmap].Assembly.Location, "System.Drawing.Common", "System.Runtime", "System.Collections", "System.Threading.Thread", "System.Threading")

function Start-Instance([string]$deskTag, [string]$media) {
    $name = "PaluneQALayers-$deskTag-$PID"
    $d = [QD]::CreateDesktopW($name, [IntPtr]::Zero, [IntPtr]::Zero, 0, 0x000F01FF, [IntPtr]::Zero)
    if ($d -eq [IntPtr]::Zero) { throw "CreateDesktop 失败：$([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
    $si = New-Object QD+STARTUPINFO
    $si.cb = [Runtime.InteropServices.Marshal]::SizeOf([type][QD+STARTUPINFO])
    $si.lpDesktop = $name
    $pi = New-Object QD+PROCESS_INFORMATION
    $cmd = "`"$Exe`" --open `"$media`""
    $ok = [QD]::CreateProcessW($Exe, $cmd, [IntPtr]::Zero, [IntPtr]::Zero, $false, 0x08000000, [IntPtr]::Zero,
        (Split-Path $Exe), [ref]$si, [ref]$pi)
    if (-not $ok) { throw "CreateProcess 失败：$([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
    $ov = [IntPtr]::Zero; $vd = [IntPtr]::Zero
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Milliseconds 500
        $wins = [QD]::DesktopWindows($d) | Where-Object { $_.Pid -eq $pi.dwProcessId -and $_.Visible -and $_.Title }
        foreach ($w in $wins) {
            if ($w.Title -like '*Video Surface*') { $vd = $w.H }
            elseif ($w.Title -like 'Palune*')      { $ov = $w.H }
        }
        if ($ov -ne [IntPtr]::Zero -and $vd -ne [IntPtr]::Zero) { break }
    }
    if ($ov -eq [IntPtr]::Zero -or $vd -eq [IntPtr]::Zero) {
        [QD]::TerminateProcess($pi.hProcess, 0) | Out-Null
        throw "未能在独立桌面上找到 Palune 的两个顶层窗口（overlay/video）"
    }
    Write-Host ("已在独立桌面 {0} 启动 Palune pid={1}（不抢前台）" -f $name, $pi.dwProcessId)
    return [pscustomobject]@{ Name = $name; Desk = $d; Pi = $pi; Overlay = $ov; Video = $vd }
}

function Shot([IntPtr]$h, [string]$path) {
    $r = New-Object QD+RECT
    [void][QD]::GetWindowRect($h, [ref]$r)
    $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($w -le 0 -or $ht -le 0) { return $null }
    $bmp = New-Object System.Drawing.Bitmap($w, $ht)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [QD]::PrintWindow($h, $hdc, [QD]::PW_RENDERFULLCONTENT)
    $g.ReleaseHdc($hdc); $g.Dispose()
    if (-not $ok) { $bmp.Dispose(); return $null }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $sum = 0.0; $n = 0; $lit = 0; $colors = @{}
    for ($y = 0; $y -lt $bmp.Height; $y += 9) {
        for ($x = 0; $x -lt $bmp.Width; $x += 9) {
            $c = $bmp.GetPixel($x, $y); $sum += ($c.R + $c.G + $c.B) / 3.0; $n++
            if (($c.R + $c.G + $c.B) -gt 24) { $lit++ }
            if ($colors.Count -lt 4000) { $colors["{0},{1},{2}" -f $c.R, $c.G, $c.B] = 1 }
        }
    }
    $bmp.Dispose()
    return [pscustomobject]@{ Path = $path; Mean = ($sum / [math]::Max($n, 1)); Colors = $colors.Count
                              NonBlack = ($lit / [math]::Max($n, 1)) }
}

# 只比中央区域，避开标题栏与窗口边框
function Measure-Diff([string]$a, [string]$b) {
    $ia = [System.Drawing.Image]::FromFile($a)
    $ib = [System.Drawing.Image]::FromFile($b)
    try {
        $w = [math]::Min($ia.Width, $ib.Width); $h = [math]::Min($ia.Height, $ib.Height)
        $x0 = [int]($w * 0.12); $x1 = [int]($w * 0.88)
        $y0 = [int]($h * 0.20); $y1 = [int]($h * 0.86)
        $ba = New-Object System.Drawing.Bitmap($ia)
        $bb = New-Object System.Drawing.Bitmap($ib)
        $diff = 0; $total = 0
        $sx = [math]::Max(1, [int](($x1 - $x0) / 320))
        $sy = [math]::Max(1, [int](($y1 - $y0) / 180))
        for ($y = $y0; $y -lt $y1; $y += $sy) {
            for ($x = $x0; $x -lt $x1; $x += $sx) {
                $ca = $ba.GetPixel($x, $y); $cb = $bb.GetPixel($x, $y)
                $total++
                if ([math]::Abs($ca.R - $cb.R) -gt 12 -or [math]::Abs($ca.G - $cb.G) -gt 12 -or
                    [math]::Abs($ca.B - $cb.B) -gt 12) { $diff++ }
            }
        }
        $ba.Dispose(); $bb.Dispose()
        return ($diff / [math]::Max($total, 1))
    } finally { $ia.Dispose(); $ib.Dispose() }
}

function ZOk {
    return ([QD]::GetWindow($video, [QD]::GW_HWNDPREV) -eq $overlay)
}

# ---------------- 呈现探针 ----------------
# 「差异 0.0000」只有在「取到的确实是逐帧呈现的内容」时才有意义，否则两边都黑
# 也会得到 0。这里用运动样片（testsrc2）跑一个独立实例，连续取样 4 次，
# 相邻取样发生变化 = 帧在推进 = 这个桌面上的呈现与抓取链路是通的。
$probeMedia = Join-Path $root 'build\qa-tmp\present-probe.mp4'
$presentEvidence = '不可用（无运动样片）'
$script:presentOk = $false
$ff = Get-Command ffmpeg -ErrorAction SilentlyContinue
if ($ff) {
    if (-not (Test-Path $probeMedia)) {
        New-Item -ItemType Directory -Force -Path (Split-Path $probeMedia) | Out-Null
        & $ff.Source -y -loglevel error -f lavfi -i "testsrc2=size=1280x720:rate=30" -t 30 `
            -pix_fmt yuv420p -c:v libx264 -preset veryfast $probeMedia
    }
} elseif (Test-Path (Join-Path $root 'qa\runtime\samples\testsrc2-720p.mp4')) {
    $probeMedia = Join-Path $root 'qa\runtime\samples\testsrc2-720p.mp4'
}
if (Test-Path $probeMedia) {
    $probe = Start-Instance 'present' $probeMedia
    # mpv 从 create 到「文件已加载」实测约 2.2s，太早取样只会拿到黑帧。
    Start-Sleep -Milliseconds 3000
    $shots = @()
    for ($k = 1; $k -le 5; $k++) {
        $shots += (Shot $probe.Video (Join-Path $Out ("present-probe-{0}.png" -f $k)))
        Start-Sleep -Milliseconds 900
    }
    $moved = 0
    for ($k = 1; $k -lt $shots.Count; $k++) {
        if ($shots[$k - 1] -and $shots[$k] -and (Measure-Diff $shots[$k - 1].Path $shots[$k].Path) -gt 0.01) { $moved++ }
    }
    $means = ($shots | ForEach-Object { '{0:N1}' -f $_.Mean }) -join '/'
    $presentEvidence = ("5 次取样相邻变化 {0}/4 次（该实例亮度 {1}）" -f $moved, $means)
    $script:presentOk = ($moved -ge 3)
    Write-Host ("呈现探针：{0}" -f $presentEvidence)
    [QD]::TerminateProcess($probe.Pi.hProcess, 0) | Out-Null
    # 等探针进程真正退出再起主实例：两者共用同一份配置/日志，上一轮实测
    # 紧挨着启动会让第二个实例卡在初始化，20 秒都建不出窗口。
    for ($w = 0; $w -lt 20; $w++) {
        if (-not (Get-Process -Id $probe.Pi.dwProcessId -ErrorAction SilentlyContinue)) { break }
        Start-Sleep -Milliseconds 250
    }
    Start-Sleep -Seconds 2
}

# ---------------- 主实例（静态测试图，用于前后比对） ----------------
$main = Start-Instance 'main' $Media
$desk = $main.Desk; $overlay = $main.Overlay; $video = $main.Video; $pi = $main.Pi

# 布局稳定后再取基线：窗口刚创建时 video 还是初始 640x480，随后布局会把它拉到整窗尺寸。
# 用早期矩形当基线，「回到原位」就永远不成立（上一版 3/3 未回基线就是这个原因），
# 覆盖窗也会按 640x480 建得比视频窗小，「完全覆盖」的前提直接不成立。
Start-Sleep -Milliseconds 2500
$r0 = New-Object QD+RECT
[void][QD]::GetWindowRect($overlay, [ref]$r0)
# 基线矩形：最小化/全屏「回到原位」的判据。窗口没回到基线说明动作没做完，
# 此时的像素差异是「还在全屏/还没还原」，不是渲染缺陷，不能计入异常。
$baseRect = New-Object QD+RECT
[void][QD]::GetWindowRect($video, [ref]$baseRect)
function Rect-Text($r) { '({0},{1})-({2},{3}) {4}x{5}' -f $r.Left, $r.Top, $r.Right, $r.Bottom, ($r.Right - $r.Left), ($r.Bottom - $r.Top) }
function Same-AsBase {
    $r = New-Object QD+RECT
    [void][QD]::GetWindowRect($video, [ref]$r)
    $same = ($r.Left -eq $baseRect.Left -and $r.Top -eq $baseRect.Top -and
             $r.Right -eq $baseRect.Right -and $r.Bottom -eq $baseRect.Bottom)
    if (-not $same) { Write-Host ("    状态未回基线：基线 {0} / 当前 {1}" -f (Rect-Text $baseRect), (Rect-Text $r)) }
    return $same
}
function RestoredOk {
    $iconicV = [QD]::IsIconic($video); $iconicO = [QD]::IsIconic($overlay)
    if ($iconicV -or $iconicO) {
        Write-Host ("    状态未还原：video 最小化={0} overlay 最小化={1}" -f $iconicV, $iconicO)
        return $false
    }
    return (Same-AsBase)
}
[QD]::StartCover($desk, ($r0.Right - $r0.Left), ($r0.Bottom - $r0.Top)) | Out-Null

# 暂停：PostMessage 空格，定向到覆盖窗，不激活
if (-not $NoPause) { [void][QD]::Key($overlay, 0x20) }
Start-Sleep -Milliseconds 1500
$s1 = Shot $video (Join-Path $Out 'hidden-pause-a.png')
Start-Sleep -Milliseconds 700
$s2 = Shot $video (Join-Path $Out 'hidden-pause-b.png')
$pauseDiff = Measure-Diff $s1.Path $s2.Path
# 「画面静止」是像素判据的硬前提：静止既可能来自暂停成功，也可能来自静态测试图。
# 从取样上无法区分这两者，也不需要区分——两种情况下前后截图都可比。
$still = $pauseDiff -lt 0.01
# 取样可用 = 不是全黑 + 不是纯色。测试图是大色块，色数本就低（实测 33），
# 不能用「色数 >200」当门（那是为真实视频定的），否则合成的干净画面会被误判不可用。
$captureUsable = ($s1.Mean -gt 3 -and $s1.Colors -gt 8)
$renderUsable = $captureUsable -and $still
Write-Host ("画面静止判定：两次取样差异={0:N4}（<0.01 视为静止）  画面可用={1}（亮度 {2:N1} / 色数 {3}）  渲染判据可用={4}" -f $pauseDiff, $captureUsable, $s1.Mean, $s1.Colors, $renderUsable)

$results = New-Object System.Collections.Generic.List[object]
function Add-Result([string]$name, [string]$verdict, [string]$struct, [string]$render, [string]$note) {
    $results.Add([pscustomobject]@{ 场景 = $name; 判定 = $verdict; 结构判据 = $struct; 渲染连续性 = $render; 说明 = $note })
}

function Run-Scenario([string]$name, [scriptblock]$act, [scriptblock]$restore, [int]$repeats, [scriptblock]$stateOk = $null, [string]$note = '', [string]$forceVerdict = '') {
    $fails = 0; $structBad = 0; $skipped = 0; $black = 0; $maxDiff = 0; $files = @(); $stat = ''
    for ($k = 1; $k -le $repeats; $k++) {
        $pre  = Join-Path $Out ("{0}-h{1}-before.png" -f $name, $k)
        $post = Join-Path $Out ("{0}-h{1}-after.png" -f $name, $k)
        $p1 = Shot $video $pre
        # 基线本身就取不到画面（近全黑）时，前后比对没有任何意义：
        # 计为「判据不成立」，避免把「两次都是黑」误读成「没有差异=通过」。
        if ($p1.Mean -lt 3) { $skipped++; $files += (Split-Path -Leaf $pre); continue }
        & $act
        Start-Sleep -Milliseconds 1200
        & $restore
        Start-Sleep -Milliseconds 2200
        $p2 = Shot $video $post
        $files += (Split-Path -Leaf $post)
        $stat = '基线 {0:N1}/非黑 {1:P0} → 结果 {2:N1}/非黑 {3:P0}' -f $p1.Mean, $p1.NonBlack, $p2.Mean, $p2.NonBlack
        # 还原后整窗取不到画面（近全黑）：这既可能是渲染真丢了，也可能是静态帧在
        # 交换链重建后没有新帧可画。两种情形必须区分，所以单独计数并写进报告，
        # 不能直接算成渲染异常。
        if ($p2.Mean -lt 3) { $black++ }
        # 结构判据与画面状态无关：z 序是窗口管理器的事实，任何一次都算数。
        if (-not (ZOk)) { $structBad++ }
        # 状态门控：窗口没回到基线（还在全屏 / 还没还原）时，像素差异无法归因，
        # 这一轮直接记「判据不成立」，不参与异常计数，也不伪装成通过。
        if ($stateOk -and -not (& $stateOk)) { $skipped++; continue }
        $d = Measure-Diff $p1.Path $p2.Path
        if ($d -gt 0.15) { $fails++ }
        $maxDiff = [math]::Max($maxDiff, $d)
    }
    $compared = $repeats - $skipped
    $renderCol = if ($skipped -eq $repeats) { '—（{0}/{1} 次状态未回到基线，未比对）' -f $skipped, $repeats }
                 elseif ($skipped -gt 0) { '{0}/{1} 次差异 >15%（最大 {2:N4}）；另 {3} 次状态未回到基线未比对' -f $fails, $compared, $maxDiff, $skipped }
                 else { '{0}/{1} 次差异 >15%（最大 {2:N4}）' -f $fails, $compared, $maxDiff }
    if ($black -gt 0) { $renderCol += ('；还原后近全黑 {0}/{1} 次' -f $black, $repeats) }
    if ($stat) { $renderCol += ('；' + $stat) }
    $verdict = if ($structBad -gt 0) { '必现（结构判据）' }
               elseif ($forceVerdict) { $forceVerdict }
               elseif (-not $renderUsable) { '未验证（渲染判据不成立）' }
               elseif ($compared -eq 0) { '未验证（判据不成立）' }
               # 没有呈现证据时，「差异 0」可能只是两边都黑，只能写「未测出问题」。
               elseif (-not $script:presentOk) { '未测出问题（无呈现证据）' }
               elseif ($fails -eq 0) { '当前构建未复现' }
               elseif ($fails -eq $compared) { '必现' } else { '偶发' }
    $noteCol = if ($note) { '{0}；证据：{1}' -f $note, ($files -join ', ') } else { ($files -join ', ') }
    Add-Result $name $verdict ("z 序异常 {0}/{1} 次" -f $structBad, $repeats) $renderCol $noteCol
}

# 1 被完全覆盖后切回（遮挡窗 = 同桌面上的纯 GDI 窗，不激活）
Run-Scenario 'covered-switchback' {
    [QD]::CoverShow($overlay, $r0)
} {
    [QD]::CoverHide()
} $Repeats $null '遮挡方是同桌面纯 GDI 窗，不等于浏览器/会议软件这类 DWM 合成窗（后者见 covered-by-browser，未验证）'

# 2 暂停后恢复（暂停态下盖住再撤下）
Run-Scenario 'pause-resume' {
    [QD]::CoverShow($overlay, $r0)
} {
    [QD]::CoverHide()
} $Repeats

# 3 切主题 A→B→C→A：PostMessage 1/2/3
# 注意：主题只作用于覆盖窗；本格的像素判据看的是视频窗，因此它只能证明
# 「切主题没有破坏视频窗渲染」，不能证明主题本身生效（结构判据仍然有效）。
Run-Scenario 'theme-cycle' {
    [void][QD]::Key($overlay, 0x31); Start-Sleep -Milliseconds 400
    [void][QD]::Key($overlay, 0x32); Start-Sleep -Milliseconds 400
} {
    [void][QD]::Key($overlay, 0x33); Start-Sleep -Milliseconds 400
    [void][QD]::Key($overlay, 0x31)
} $Repeats $null '主题作用于覆盖窗；像素判据看视频窗，仅能证明视频窗未被主题切换破坏'

# 4 全屏进出：PostMessage F11（定向，不激活）
# 状态门控：全屏没退干净时画面必然不同，这类「差异」不能算渲染缺陷。
# 另外，全屏会重建视频表面：独立桌面没有合成器驱动重绘，还原后取到的是
# 空白/缩放前的旧内容（实测亮度 155、色数 33，与基线 1023x569 的帧不符），
# 无法归因，故本格只出结构判据。
Run-Scenario 'fullscreen-roundtrip' {
    [void][QD]::Key($overlay, 0x7A)
} {
    [void][QD]::Key($overlay, 0x7A)
} $Repeats { Same-AsBase } '全屏会重建视频表面，独立桌面无合成器驱动重绘，还原后的画面不可比' '未验证（还原后取不到可比画面）'

# 5 最小化还原（SW_MINIMIZE 不抢焦点；还原用 SW_SHOWNOACTIVATE）
# 同上：最小化销毁交换链，独立桌面上没有新帧驱动重绘，还原后整窗近全黑
# （实测亮度 <3）。这只说明「隐藏桌面上取不到还原后的画面」，不能当成渲染缺陷，
# 也不能因为「两次都是黑、差异为 0」就算通过——故只出结构判据。
Run-Scenario 'minimize-restore' {
    [void][QD]::ShowWindow($overlay, [QD]::SW_MINIMIZE)
    [void][QD]::ShowWindow($video, [QD]::SW_MINIMIZE)
} {
    [void][QD]::ShowWindow($overlay, [QD]::SW_SHOWNOACTIVATE)
    [void][QD]::ShowWindow($video, [QD]::SW_SHOWNOACTIVATE)
    [QD]::CoverHide()
} $Repeats { RestoredOk } '最小化销毁交换链，独立桌面无新帧驱动重绘，还原后近全黑，画面不可比' '未验证（还原后取不到可比画面）'

# 6 Alt+Tab：隐藏桌面无法产生真实前景切换，按要求不做、不回落
Add-Result 'alt-tab-roundtrip' '未在真实前台验证' '—' '—' `
    'Alt+Tab 的语义就是前景切换，独立桌面上无法产生；按要求不用 SetForegroundWindow 伪造，也不回落用户桌面补数'

# 7 真实合成窗遮挡：Chrome 在独立桌面上无法保证起窗/DWM 合成，不回落用户桌面
Add-Result 'covered-by-browser' '未在真实前台验证' '—' '—' `
    '浏览器遮挡依赖 DWM 合成层，隐藏桌面观测不到合成结果；按要求不在用户桌面补跑'

# 8-10 不具备真实条件的三格
Add-Result 'cross-screen-move' '未验证' '—' '—' '本机只有 1 台显示器，不具备触发条件（不计入通过）'
Add-Result 'dpi-change' '未验证' '—' '—' '没有第二台不同 DPI 的显示器；注入 WM_DPICHANGED 不是真实条件（本轮未注入）'
Add-Result 'sleep-wake' '未验证' '—' '—' '真休眠会中断整机；注入 PBT_APMSUSPEND 不是真实条件（本轮未注入）'

# ---------------------------------------------------------------- 收尾
if (-not $KeepRunning) {
    [QD]::TerminateProcess($pi.hProcess, 0) | Out-Null
}
[QD]::CloseHandle($pi.hProcess) | Out-Null
[QD]::CloseHandle($pi.hThread) | Out-Null
[QD]::CloseDesktop($desk) | Out-Null

# ---------------------------------------------------------------- 报告
$md = @()
$md += '# 覆盖/还原类渲染缺陷：独立桌面逐格取证（不抢前台）'
$md += ''
$md += ("- 生成时间：{0}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
$md += ("- 被测程序：``{0}``（pid {1}）" -f $Exe, $pi.dwProcessId)
$md += ("- 运行位置：独立桌面 ``{0}``（CreateDesktop + STARTUPINFO.lpDesktop），全程不激活、不切前台" -f $deskName)
$md += '- 按键：一律 `PostMessage(WM_KEYDOWN/WM_KEYUP)` 定向到覆盖窗 hwnd，不使用任何真实前台按键序列'
$md += '- 取像素：`PrintWindow(PW_RENDERFULLCONTENT)` 直接取窗口自身像素，不依赖屏幕合成'
$md += ("- 测试图：``{0}``（静态彩条，mpv keep-open 停在最后一帧）。动态样片每帧都在变，前后比对不成立" -f (Split-Path -Leaf $Media))
$md += ''
$md += '## 判据能说明什么、不能说明什么'
$md += ''
$md += '- 能说明：窗口自身是否仍在正常渲染（扰动前后像素差异）、双窗 z 序是否仍满足「视频窗紧贴覆盖窗正下方」、窗口是否仍可见。'
$md += '- 不能说明：用户肉眼看到的**屏幕合成层**（遮挡窗/浏览器撤走后的残留像素）。隐藏桌面不参与 DWM 屏幕合成，该维度必须真实前台，本报告一律标「未在真实前台验证」。'
$md += ("- 取样门控：两次取样差异 {0:N4}（<0.01 视为画面静止），平均亮度 {1:N1}、色数 {2}；渲染判据{3}" -f $pauseDiff, $s1.Mean, $s1.Colors, $(if ($renderUsable) { '成立' } else { '**不成立**（画面未静止或取样全黑），相关格只保留结构判据' }))
$md += ("- 呈现证据：{0}。相邻取样无变化时，「差异 0」分不清「画面对」与「两边都黑」，只能写「未测出问题」" -f $presentEvidence)
$md += ''
$md += '| 场景 | 判定 | 结构判据 | 渲染连续性 | 说明 |'
$md += '| --- | --- | --- | --- | --- |'
foreach ($r in $results) { $md += ("| {0} | {1} | {2} | {3} | {4} |" -f $r.场景, $r.判定, $r.结构判据, $r.渲染连续性, $r.说明) }
$md += ''
$md += '## 口径'
$md += ''
$md += '- 跨屏 / DPI / 休眠三格记「未验证」，单列，不计入通过。'
$md += '- Alt+Tab 与浏览器遮挡两格记「未在真实前台验证」，不计入通过。'
$md += '- 未做修正前代码 A/B，通过项一律表述为「当前构建未复现」，不是「已修复」。'

$md -join "`r`n" | Set-Content -Path (Join-Path $Out 'report-hidden.md') -Encoding UTF8
$results | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $Out 'result-hidden.json') -Encoding UTF8
Write-Host ("报告：{0}" -f (Join-Path $Out 'report-hidden.md'))
$results | Format-Table -AutoSize | Out-String | Write-Host
