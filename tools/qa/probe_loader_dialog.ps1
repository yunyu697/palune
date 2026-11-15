#requires -Version 5.1
<#
.SYNOPSIS
  单次、可控地复现「Windows 加载器缺失依赖」对话框并读取原文。
  每次运行只测一个变体（由 -Variant 指定），避免多个 csrss 错误框互相排队导致误读。
#>
param(
    [ValidateSet('none', 'no-quick', 'no-widgets', 'no-qwindows')][string]$Variant = 'no-quick',
    [string]$Out = ""
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$src = Join-Path $root 'build\Release'
if (-not $Out) { $Out = Join-Path $root 'qa\evidence\copies' }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Add-Type -AssemblyName System.Drawing.Common
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$cs = @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public class U7 {
  public delegate bool P(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(P cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RC r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RC { public int L,T,R,B; }
  public static List<string> Entry(IntPtr h){
    uint p; GetWindowThreadProcessId(h, out p);
    var t=new StringBuilder(512); GetWindowTextW(h,t,512);
    var c=new StringBuilder(64); GetClassNameW(h,c,64);
    return new List<string>{ h.ToInt64()+"", p+"", c.ToString(), t.ToString(), IsWindowVisible(h)?"1":"0" };
  }
  public static List<List<string>> Windows(){
    var o=new List<List<string>>();
    EnumWindows((h,l)=>{ var t=new StringBuilder(4); GetWindowTextW(h,t,4); if(t.Length>0) o.Add(Entry(h)); return true; }, IntPtr.Zero);
    return o;
  }
  public static void Close(IntPtr h){ PostMessageW(h,0x0010,IntPtr.Zero,IntPtr.Zero); }
  public static int[] Rect(IntPtr h){ var r=new RC(); GetWindowRect(h,out r); return new int[]{r.L,r.T,r.R-r.L,r.B-r.T}; }
}
'@
Add-Type -TypeDefinition $cs | Out-Null

function Get-ErrorDialogs {
    $list = @()
    foreach ($w in [U7]::Windows()) {
        if ($w[2] -eq '#32770' -and ($w[3] -like '*系统错误*')) { $list += , $w }
    }
    return $list
}

function Close-ErrorDialog([IntPtr]$hwnd) {
    # csrss 持有的硬错误框不响应 WM_CLOSE，必须点它的「确定」按钮。
    try {
        $el = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
        $btnCond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::Button)
        $btn = $el.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $btnCond)
        if ($btn) {
            $pat = $btn.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
            $pat.Invoke()
            return
        }
    } catch {}
    [U7]::Close($hwnd)
}

# 1) 清理历史遗留的「…系统错误」框（csrss 持有、进程已退出，属无主残留）
$before = Get-ErrorDialogs
foreach ($w in $before) { Close-ErrorDialog([IntPtr][int64]$w[0]) }
Start-Sleep -Milliseconds 1500
$left = Get-ErrorDialogs
Write-Host ("清理前残留 {0} 个，清理后 {1} 个" -f $before.Count, $left.Count)
foreach ($w in $left) { Write-Host ("  仍残留：pid={0} title={1} vis={2}" -f $w[1], $w[3], $w[4]) }
if ($left.Count -gt 0) { Close-ErrorDialog([IntPtr][int64]$left[0][0]); Start-Sleep -Milliseconds 800 }

# 2) 准备变体目录
$work = Join-Path $root "qa\runtime\repro-loader\$Variant"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
Copy-Item -Recurse -Force $src $work
switch ($Variant) {
    'none'       { @('Qt6Widgets.dll','Qt6Quick.dll','Qt6Gui.dll','Qt6Qml.dll','Qt6Network.dll','Qt6Core.dll') | ForEach-Object { Remove-Item -Force (Join-Path $work $_) -ErrorAction SilentlyContinue } }
    'no-quick'   { Remove-Item -Force (Join-Path $work 'Qt6Quick.dll') -ErrorAction SilentlyContinue }
    'no-widgets' { Remove-Item -Force (Join-Path $work 'Qt6Widgets.dll') -ErrorAction SilentlyContinue }
    'no-qwindows'{ Remove-Item -Recurse -Force (Join-Path $work 'platforms') -ErrorAction SilentlyContinue }
}

# 3) 启动并等它自己的对话框
$proc = Start-Process -FilePath (Join-Path $work 'Palune.exe') -WorkingDirectory $work -PassThru
$dlg = $null
for ($i = 0; $i -lt 80; $i++) {
    foreach ($c in Get-ErrorDialogs) {
        # 只认真正显示出来（rect 非空）的对话框，排队未显示的不算。
        $r = [U7]::Rect([IntPtr][int64]$c[0])
        if ($r[2] -gt 50 -and $r[3] -gt 30) { $dlg = $c; break }
    }
    if ($dlg) { break }
    if ($proc.HasExited) { break }
    Start-Sleep -Milliseconds 250
}

$title = ''; $body = ''; $shot = ''; $hostPid = ''
if ($dlg) {
    $title = $dlg[3]; $hostPid = $dlg[1]
    try {
        $el = [System.Windows.Automation.AutomationElement]::FromHandle([IntPtr][int64]$dlg[0])
        $all = $el.FindAll([System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.Condition]::TrueCondition)
        $texts = @()
        foreach ($e in $all) {
            $n = $e.Current.Name
            if ($n -and $n -ne '确定' -and $n -ne 'OK') { $texts += $n }
        }
        $body = ($texts -join ' / ')
    } catch { $body = "UIA 读取失败：$_" }
    $r = [U7]::Rect([IntPtr][int64]$dlg[0])
    $bmp = New-Object System.Drawing.Bitmap($r[2], $r[3])
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r[0], $r[1], 0, 0, (New-Object System.Drawing.Size($r[2], $r[3])))
    $g.Dispose()
    $shot = "loader-$Variant.png"
    $bmp.Save((Join-Path $Out $shot)); $bmp.Dispose()
    [U7]::Close([IntPtr][int64]$dlg[0])
}
Start-Sleep -Milliseconds 600
$exited = $proc.HasExited
$code = if ($exited) { ('0x{0:X8}' -f $proc.ExitCode) } else { 'RUNNING' }
if (-not $exited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
Start-Sleep -Milliseconds 400

$removedText = switch ($Variant) {
    'none' { 'Qt6Widgets/Quick/Gui/Qml/Network/Core.dll' }
    'no-quick' { 'Qt6Quick.dll' }
    'no-widgets' { 'Qt6Widgets.dll' }
    'no-qwindows' { 'platforms\' }
}
$row = [pscustomobject]@{
    variant = $Variant
    removed = $removedText
    processId = $proc.Id; dialogHostPid = $hostPid; exitCode = $code
    dialogTitle = $title; dialogBody = $body; screenshot = $shot
    capturedAt = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
}
$jsonPath = Join-Path $Out 'loader-chain.json'
$all = @()
if (Test-Path $jsonPath) { $all = @(Get-Content $jsonPath -Raw | ConvertFrom-Json) }
$all = $all | Where-Object { $_.variant -ne $Variant }
$all += $row
$all | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $jsonPath
Write-Host ("[{0}] exit={1} host={2} title='{3}' body='{4}'" -f $Variant, $code, $hostPid, $title, $body)
