# 收尾：清掉 QA 留下的 Palune / tst_* 进程，以及 csrss 替它们弹出的系统错误框。
#
# 只关闭「标题或正文里出现 Palune / tst_ / Qt6」的对话框，避免误关别家程序的框。
# 只做关闭动作，不激活任何窗口、不切前台。

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$cs = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class CW {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    public static string Title(IntPtr h) { var sb = new StringBuilder(512); GetWindowTextW(h, sb, sb.Capacity); return sb.ToString(); }
    public static string Cls(IntPtr h) { var sb = new StringBuilder(256); GetClassNameW(h, sb, sb.Capacity); return sb.ToString(); }
    public static List<IntPtr> Dialogs() {
        var list = new List<IntPtr>();
        EnumWindows((h, l) => {
            if (IsWindowVisible(h) && Cls(h) == "#32770") list.Add(h);
            return true;
        }, IntPtr.Zero);
        return list;
    }
}
'@
Add-Type -TypeDefinition $cs

function Get-AllText([System.Windows.Automation.AutomationElement]$root) {
    $sb = New-Object System.Text.StringBuilder
    $cond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Text)
    foreach ($e in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)) {
        [void]$sb.AppendLine($e.Current.Name)
    }
    return $sb.ToString()
}

$closed = 0; $killed = 0
foreach ($h in [CW]::Dialogs()) {
    $title = [CW]::Title($h)
    $text = ''
    try {
        $el = [System.Windows.Automation.AutomationElement]::FromHandle($h)
        $text = Get-AllText $el
    } catch { }
    $blob = "$title`n$text"
    if ($blob -notmatch 'Palune|tst_|Qt6') { continue }

    Write-Host ("关闭对话框：{0}  <{1}>" -f $title, (($text -split "`r?`n" | Where-Object { $_.Trim() } | Select-Object -First 2) -join ' / '))
    $done = $false
    try {
        $el = [System.Windows.Automation.AutomationElement]::FromHandle($h)
        $btnCond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::Button)
        $btn = $el.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $btnCond)
        if ($btn) {
            $inv = $btn.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
            $inv.Invoke()
            $done = $true
        }
    } catch { }
    if (-not $done) { [CW]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }
    $closed++
    Start-Sleep -Milliseconds 250
}

Get-Process | Where-Object { $_.ProcessName -like 'Palune*' -or $_.ProcessName -like 'tst_*' } | ForEach-Object {
    Write-Host ("结束进程：{0} pid={1}" -f $_.ProcessName, $_.Id)
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    $killed++
}

Write-Host ("收尾完成：关闭对话框 {0} 个，结束进程 {1} 个" -f $closed, $killed)
