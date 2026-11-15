#requires -Version 5.1
<#
.SYNOPSIS
  受控复现「Windows 加载器缺失依赖」对话框，验证「链式逐个报不同 DLL」的成因。

  做三个变体（都从 build\Release 完整部署树复制，只删特定文件）：
    none        —— 不含任何 Qt6*.dll        → 期望报第一个导入项 Qt6Widgets.dll
    no-quick    —— 删掉 Qt6Quick.dll        → 期望报 Qt6Quick.dll（与用户截图一致）
    no-widgets  —— 删掉 Qt6Widgets.dll      → 期望报 Qt6Widgets.dll

  每个变体真启动一次，抓 #32770 对话框标题 + UIA 正文原文 + 截图，然后关掉对话框、结束进程。
#>
param([string]$Out = "")

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$src = Join-Path $root 'build\Release'
if (-not $Out) { $Out = Join-Path $root 'qa\evidence\copies' }
$work = Join-Path $root 'qa\runtime\repro-loader'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Add-Type -AssemblyName System.Drawing.Common
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$cs = @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public class L4 { public delegate bool P(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(P cb, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
[DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RC r);
[StructLayout(LayoutKind.Sequential)] public struct RC { public int L,T,R,B; }
public static List<IntPtr> Dialogs(){ var o=new List<IntPtr>(); EnumWindows((h,l)=>{ var c=new StringBuilder(64); GetClassNameW(h,c,64); var t=new StringBuilder(512); GetWindowTextW(h,t,512); if(c.ToString()=="#32770" && t.ToString().StartsWith("Palune.exe")) o.Add(h); return true; }, IntPtr.Zero); return o; }
public static string Title(IntPtr h){ var t=new StringBuilder(512); GetWindowTextW(h,t,512); return t.ToString(); }
public static void Close(IntPtr h){ PostMessageW(h,0x0010,IntPtr.Zero,IntPtr.Zero); }
public static int[] Rect(IntPtr h){ var r=new RC(); GetWindowRect(h,out r); return new int[]{r.L,r.T,r.R-r.L,r.B-r.T}; }
}
'@
Add-Type -TypeDefinition $cs | Out-Null

if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force -Path $work | Out-Null

$variants = @(
    @{ name = 'none';       remove = @('Qt6Widgets.dll', 'Qt6Quick.dll', 'Qt6Gui.dll', 'Qt6Qml.dll', 'Qt6Network.dll', 'Qt6Core.dll') },
    @{ name = 'no-quick';   remove = @('Qt6Quick.dll') },
    @{ name = 'no-widgets'; remove = @('Qt6Widgets.dll') }
)

$rows = @()
# 先把桌面上历史遗留的 Palune 加载器错误框全部关掉，否则会读成上一个对话框的内容。
# （只关这种无主错误框，不动用户任何文件）
foreach ($d in [L4]::Dialogs()) { [L4]::Close($d) }
Start-Sleep -Milliseconds 800
$preLeft = ([L4]::Dialogs()).Count

foreach ($v in $variants) {
    $dir = Join-Path $work $v.name
    Copy-Item -Recurse -Force $src $dir
    foreach ($f in $v.remove) {
        Remove-Item -Force (Join-Path $dir $f) -ErrorAction SilentlyContinue
    }
    $before = ([L4]::Dialogs()).Count
    # 这些变体只用于复现加载器行为，不做任何其它事。
    $proc = Start-Process -FilePath (Join-Path $dir 'Palune.exe') -WorkingDirectory $dir -PassThru
    $dlg = [IntPtr]::Zero
    for ($i = 0; $i -lt 40; $i++) {
        $found = [L4]::Dialogs()
        if ($found.Count -gt $before) { $dlg = $found[-1]; break }
        if ($proc.HasExited) { break }
        Start-Sleep -Milliseconds 250
    }
    $title = ''; $body = ''; $shot = ''
    if ($dlg -ne [IntPtr]::Zero) {
        $title = [L4]::Title($dlg)
        try {
            $el = [System.Windows.Automation.AutomationElement]::FromHandle($dlg)
            $all = $el.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                [System.Windows.Automation.Condition]::TrueCondition)
            $texts = @()
            foreach ($e in $all) {
                $n = $e.Current.Name
                if ($n -and $n -ne '确定' -and $n -ne 'OK') { $texts += $n }
            }
            $body = ($texts -join ' / ')
        } catch { $body = "UIA 读取失败：$_" }
        $r = [L4]::Rect($dlg)
        $bmp = New-Object System.Drawing.Bitmap($r[2], $r[3])
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($r[0], $r[1], 0, 0, (New-Object System.Drawing.Size($r[2], $r[3])))
        $g.Dispose()
        $shot = "loader-$($v.name).png"
        $bmp.Save((Join-Path $Out $shot))
        $bmp.Dispose()
        [L4]::Close($dlg)
        Start-Sleep -Milliseconds 400
    }
    Start-Sleep -Milliseconds 800
    $exited = $proc.HasExited
    $code = if ($exited) { ('0x{0:X8}' -f $proc.ExitCode) } else { 'RUNNING' }
    if (-not $exited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    $rows += [pscustomobject]@{
        variant = $v.name; removed = ($v.remove -join ','); exitCode = $code
        dialogTitle = $title; dialogBody = $body; screenshot = $shot
    }
    Write-Host ("[{0}] exit={1} dialog='{2}' body='{3}'" -f $v.name, $code, $title, $body)
    Start-Sleep -Milliseconds 600
}

$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$rows | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $Out 'loader-chain.json')

$md = @()
$md += "# 加载器缺失依赖对话框的受控复现"
$md += ""
$md += "- 生成时间：$stamp"
$md += "- 方法：从 ``build\Release``（完整部署树）复制，仅删除指定 DLL，然后按「工作目录 = exe 目录」启动。"
$md += ""
$md += "| 变体 | 删除的文件 | 退出码 | 对话框标题 | 对话框正文 | 截图 |"
$md += "| --- | --- | --- | --- | --- | --- |"
foreach ($r in $rows) {
    $md += ("| {0} | {1} | {2} | {3} | {4} | {5} |" -f $r.variant, $r.removed, $r.exitCode, $r.dialogTitle,
        ($r.dialogBody -replace '\|', '/'), $r.screenshot)
}
$md | Set-Content -Encoding UTF8 (Join-Path $Out 'loader-chain.md')
