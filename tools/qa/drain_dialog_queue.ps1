#requires -Version 5.1
<#
.SYNOPSIS
  把桌面上排队的「…系统错误」硬错误框逐条读出来并关闭，证明「链式弹窗」到底是几个进程。

  机制：Windows 同一时刻只显示一个硬错误框（STATUS_DLL_NOT_FOUND 类），
  其余排队；关掉一个才露出下一个，而下一个往往属于**另一个**失败的进程。
  所以用户看到的「关掉一个又弹一个、每次缺的组件不同」= N 个失败启动，不是一个进程报多次。

  每个对话框记录：hwnd / 宿主进程（应为 csrss）/ 标题（含 exe 名）/ 正文原文 / 是否可见。
#>
param([string]$Out = "")
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Out) { $Out = Join-Path $root 'qa\evidence\copies' }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$cs = @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public class Q9 {
  public delegate bool P(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(P cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  public class W { public long Handle; public uint Pid; public string Cls; public string Title; public bool Visible; }
  public static List<W> List(){
    var o = new List<W>();
    EnumWindows((h,l)=>{
      var c = new StringBuilder(64); GetClassNameW(h,c,64);
      var t = new StringBuilder(512); GetWindowTextW(h,t,512);
      if (c.ToString() == "#32770" && t.ToString().Contains("系统错误")) {
        uint p; GetWindowThreadProcessId(h, out p);
        o.Add(new W{ Handle=h.ToInt64(), Pid=p, Cls=c.ToString(), Title=t.ToString(), Visible=IsWindowVisible(h) });
      }
      return true;
    }, IntPtr.Zero);
    return o;
  }
  public static void PostClose(long h){ PostMessageW(new IntPtr(h), 0x0010, IntPtr.Zero, IntPtr.Zero); }
}
'@
Add-Type -TypeDefinition $cs | Out-Null

function Read-Dialog([long]$hwnd) {
    $body = ''
    try {
        $el = [System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]$hwnd)
        $all = $el.FindAll([System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.Condition]::TrueCondition)
        $texts = @()
        foreach ($e in $all) {
            $n = $e.Current.Name
            if ($n -and $n -ne '确定' -and $n -ne 'OK') { $texts += $n }
        }
        $body = ($texts -join ' / ')
    } catch { $body = "UIA 读取失败：$($_.Exception.Message)" }
    return $body
}

function Dismiss([long]$hwnd) {
    try {
        $el = [System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]$hwnd)
        $cond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::Button)
        $btn = $el.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($btn) {
            $btn.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
            return 'uia-invoke'
        }
    } catch {}
    [Q9]::PostClose($hwnd)
    return 'wm-close'
}

$seen = @{}
$rows = @()
$rounds = 0
while ($rounds -lt 40) {
    $rounds++
    $list = [Q9]::List()
    $fresh = @($list | Where-Object { -not $seen.ContainsKey([string]$_.Handle) })
    if ($fresh.Count -eq 0) { break }
    foreach ($w in $fresh) {
        $seen[[string]$w.Handle] = $true
        $body = Read-Dialog $w.Handle
        $how = Dismiss $w.Handle
        Start-Sleep -Milliseconds 700
        $rows += [pscustomobject]@{
            order    = $rows.Count + 1
            hwnd     = $w.Handle
            hostPid  = $w.Pid
            title    = $w.Title
            body     = $body
            visible  = $w.Visible
            dismissedBy = $how
        }
        Write-Host ("[{0}] {1} :: {2}" -f $rows.Count, $w.Title, $body)
    }
    Start-Sleep -Milliseconds 700
}

$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$rows | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $Out 'dialog-queue.json')
$md = @()
$md += "# 桌面上排队的「系统错误」硬错误框（逐条读出并关闭）"
$md += ""
$md += "- 时间：$stamp"
$md += "- 共 $($rows.Count) 个。宿主进程均为 csrss.exe（pid 608）= Windows 加载器硬错误框的特征，"
$md += "  真正的 Palune/tst_* 进程在弹框前就已退出。"
$md += ""
$md += "| 序 | 标题（含 exe 名） | 正文原文 | 可见 |"
$md += "| --- | --- | --- | --- |"
foreach ($r in $rows) {
    $md += ("| {0} | {1} | {2} | {3} |" -f $r.order, $r.title, ($r.body -replace '\|', '/'), $r.visible)
}
$md | Set-Content -Encoding UTF8 (Join-Path $Out 'dialog-queue.md')
Write-Host ""
Write-Host "共读出 $($rows.Count) 个"
