#Requires -Version 5.1
param(
  [string]$DemoExe = "$PSScriptRoot\..\build-surfaces-demo-smoke-release\examples\surfaces_demo\windows\Release\win32_surfaces_demo.exe",
  [string]$LoadOnlyExe = "$PSScriptRoot\..\build-surfaces-demo-smoke-release\examples\surfaces_demo\windows\Release\win32_surfaces_demo_load_only.exe",
  [string]$StateDir = "$PSScriptRoot\..\smoke_profiles\surfaces_demo_smoke_20261018",
  [string]$ArtifactDir = "$PSScriptRoot\..\docs\surfaces_demo_smoke_artifacts"
)

$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections.Generic;
public static class Win32Smoke {
  public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
  [DllImport("user32.dll", SetLastError=true)] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, int dwExtraInfo);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
  public const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
  public const uint MOUSEEVENTF_LEFTUP = 0x0004;
  public const uint WM_CLOSE = 0x0010;
  public const string SurfaceClass = "AppTraverseSurfacesWindow";
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  public static List<IntPtr> FindSurfaceWindows(uint pid) {
    var list = new List<IntPtr>();
    EnumWindows((h, _) => {
      uint wpid;
      GetWindowThreadProcessId(h, out wpid);
      if (wpid != pid) return true;
      var sb = new StringBuilder(256);
      GetClassNameW(h, sb, sb.Capacity);
      if (sb.ToString() == SurfaceClass && IsWindowVisible(h)) list.Add(h);
      return true;
    }, IntPtr.Zero);
    list.Sort((a,b) => {
      RECT ra, rb;
      GetWindowRect(a, out ra); GetWindowRect(b, out rb);
      return ra.Left.CompareTo(rb.Left);
    });
    return list;
  }
  public static string RectStr(IntPtr h) {
    RECT r; GetWindowRect(h, out r);
    return string.Format("({0},{1}) {2}x{3}", r.Left, r.Top, r.Right-r.Left, r.Bottom-r.Top);
  }
}
"@

function Wait-SurfaceWindows {
  param([System.Diagnostics.Process]$Proc, [int]$MinCount = 1, [int]$TimeoutSec = 60)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if ($Proc.HasExited) { throw "Process exited early code $($Proc.ExitCode)" }
    $w = [Win32Smoke]::FindSurfaceWindows([uint32]$Proc.Id)
    if ($w.Count -ge $MinCount) { return $w }
    Start-Sleep -Milliseconds 200
  }
  throw "Timeout waiting for $MinCount surface window(s)"
}

function Sample-WindowMotion {
  param([IntPtr]$Hwnd, [int]$DurationMs, [int]$IntervalMs = 50)
  $samples = New-Object System.Collections.Generic.List[object]
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  while ($sw.ElapsedMilliseconds -lt $DurationMs) {
    $r = New-Object Win32Smoke+RECT
    [Win32Smoke]::GetWindowRect($Hwnd, [ref]$r) | Out-Null
    $samples.Add([pscustomobject]@{
      phase = $args[0]
      t_ms = $sw.ElapsedMilliseconds
      left = $r.Left; top = $r.Top
      w = $r.Right - $r.Left; h = $r.Bottom - $r.Top
    })
    Start-Sleep -Milliseconds $IntervalMs
  }
  return $samples
}

function Analyze-MotionSamples {
  param($Samples, [string]$Label)
  $jumps = @()
  $maxDelta = 0
  for ($i = 1; $i -lt $Samples.Count; $i++) {
    $dl = [Math]::Abs($Samples[$i].left - $Samples[$i-1].left)
    $dt = [Math]::Abs($Samples[$i].top - $Samples[$i-1].top)
    $d = [Math]::Max($dl, $dt)
    if ($d -gt $maxDelta) { $maxDelta = $d }
    if ($dl -gt 40 -or $dt -gt 40) {
      $jumps += [pscustomobject]@{ i=$i; dt_ms=($Samples[$i].t_ms - $Samples[$i-1].t_ms); dl=$dl; dt=$dt }
    }
  }
  [pscustomobject]@{ label=$Label; samples=$Samples.Count; max_step_px=$maxDelta; jump_count_over_40px=$jumps.Count; jumps=$jumps }
}

function Click-AddOnWindow {
  param([IntPtr]$Hwnd)
  [Win32Smoke]::SetForegroundWindow($Hwnd) | Out-Null
  Start-Sleep -Milliseconds 150
  $pt = New-Object Win32Smoke+POINT
  $pt.X = 52; $pt.Y = 26
  [Win32Smoke]::ClientToScreen($Hwnd, [ref]$pt) | Out-Null
  [Win32Smoke]::SetCursorPos($pt.X, $pt.Y) | Out-Null
  Start-Sleep -Milliseconds 50
  [Win32Smoke]::mouse_event([Win32Smoke]::MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
  Start-Sleep -Milliseconds 30
  [Win32Smoke]::mouse_event([Win32Smoke]::MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
  Start-Sleep -Milliseconds 500
}

function Run-TitleDrag {
  param([IntPtr]$Hwnd, [int]$DurationMs)
  [Win32Smoke]::SetForegroundWindow($Hwnd) | Out-Null
  Start-Sleep -Milliseconds 100
  $r = New-Object Win32Smoke+RECT
  [Win32Smoke]::GetWindowRect($Hwnd, [ref]$r) | Out-Null
  $baseX = [int](($r.Left + $r.Right) / 2)
  $baseY = $r.Top + 12
  [Win32Smoke]::SetCursorPos($baseX, $baseY) | Out-Null
  Start-Sleep -Milliseconds 100
  [Win32Smoke]::mouse_event([Win32Smoke]::MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  while ($sw.ElapsedMilliseconds -lt $DurationMs) {
    $t = $sw.ElapsedMilliseconds
    $phase = $t / 8000.0 * [Math]::PI * 2
    $x = $baseX + [int](120 * [Math]::Sin($phase))
    $y = $baseY + [int](80 * [Math]::Cos($phase * 0.7))
    [Win32Smoke]::SetCursorPos($x, $y) | Out-Null
    Start-Sleep -Milliseconds 16
  }
  [Win32Smoke]::mouse_event([Win32Smoke]::MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
}

function Run-TitleDragOffset {
  param([IntPtr]$Hwnd, [int]$DurationMs, [int]$OffsetX)
  [Win32Smoke]::SetForegroundWindow($Hwnd) | Out-Null
  Start-Sleep -Milliseconds 80
  $r = New-Object Win32Smoke+RECT
  [Win32Smoke]::GetWindowRect($Hwnd, [ref]$r) | Out-Null
  $startX = [int](($r.Left + $r.Right) / 2)
  $startY = $r.Top + 12
  $endX = $startX + $OffsetX
  [Win32Smoke]::SetCursorPos($startX, $startY) | Out-Null
  Start-Sleep -Milliseconds 80
  [Win32Smoke]::mouse_event([Win32Smoke]::MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  while ($sw.ElapsedMilliseconds -lt $DurationMs) {
    $frac = [Math]::Min(1.0, $sw.ElapsedMilliseconds / [double]$DurationMs)
    $x = [int]($startX + $OffsetX * $frac)
    [Win32Smoke]::SetCursorPos($x, $startY) | Out-Null
    Start-Sleep -Milliseconds 16
  }
  [Win32Smoke]::mouse_event([Win32Smoke]::MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
}

function Resize-BottomRight {
  param([IntPtr]$Hwnd, [int]$DeltaW, [int]$DeltaH)
  [Win32Smoke]::SetForegroundWindow($Hwnd) | Out-Null
  $r = New-Object Win32Smoke+RECT
  [Win32Smoke]::GetWindowRect($Hwnd, [ref]$r) | Out-Null
  $x = $r.Right - 4; $y = $r.Bottom - 4
  [Win32Smoke]::SetCursorPos($x, $y) | Out-Null
  Start-Sleep -Milliseconds 80
  [Win32Smoke]::mouse_event([Win32Smoke]::MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
  for ($i = 1; $i -le 20; $i++) {
    [Win32Smoke]::SetCursorPos($x + [int]($DeltaW * $i / 20), $y + [int]($DeltaH * $i / 20)) | Out-Null
    Start-Sleep -Milliseconds 25
  }
  [Win32Smoke]::mouse_event([Win32Smoke]::MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
  Start-Sleep -Milliseconds 200
}

function Stop-DemoGracefully {
  param([System.Diagnostics.Process]$Proc)
  $w = [Win32Smoke]::FindSurfaceWindows([uint32]$Proc.Id)
  if ($w.Count -eq 0) { if (-not $Proc.HasExited) { $Proc | Stop-Process -Force }; return }
  [Win32Smoke]::PostMessageW($w[0], [Win32Smoke]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  if (-not $Proc.WaitForExit(15000)) { $Proc | Stop-Process -Force; throw "Graceful stop timeout" }
}

New-Item -ItemType Directory -Force -Path $ArtifactDir | Out-Null
New-Item -ItemType Directory -Force -Path $StateDir | Out-Null

$repoRoot = Split-Path $PSScriptRoot -Parent
$report = [ordered]@{}
$report.source_sha = (git -C $repoRoot rev-parse HEAD).Trim()
$report.demo_exe = (Resolve-Path $DemoExe).Path
$report.load_only_exe = (Resolve-Path $LoadOnlyExe).Path
$report.state_dir = (Resolve-Path $StateDir).Path
$report.demo_sha256 = (Get-FileHash -Algorithm SHA256 $DemoExe).Hash
$report.window_class = 'AppTraverseSurfacesWindow'

Write-Host "Launching demo..."
$proc = Start-Process -FilePath $DemoExe -ArgumentList @('--state-dir', $StateDir) -PassThru
$windows = Wait-SurfaceWindows -Proc $proc -MinCount 1
Write-Host "Surface 1: $([Win32Smoke]::RectStr($windows[0])) PID=$($proc.Id)"

Click-AddOnWindow -Hwnd $windows[0]
$windows = Wait-SurfaceWindows -Proc $proc -MinCount 2
Write-Host "Two surfaces: $($windows | ForEach-Object { [Win32Smoke]::RectStr($_) })"

$target = $windows[0]
$motionLog = New-Object System.Collections.Generic.List[object]

Write-Host "60s title-bar drag..."
[Win32Smoke]::SetForegroundWindow($target) | Out-Null
Start-Sleep -Milliseconds 100
$r0 = New-Object Win32Smoke+RECT
[Win32Smoke]::GetWindowRect($target, [ref]$r0) | Out-Null
$baseX = [int](($r0.Left + $r0.Right) / 2)
$baseY = $r0.Top + 12
[Win32Smoke]::SetCursorPos($baseX, $baseY) | Out-Null
Start-Sleep -Milliseconds 100
[Win32Smoke]::mouse_event([Win32Smoke]::MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
$dragSamples = New-Object System.Collections.Generic.List[object]
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.ElapsedMilliseconds -lt 60000) {
  $t = $sw.ElapsedMilliseconds
  $phase = $t / 8000.0 * [Math]::PI * 2
  $x = $baseX + [int](120 * [Math]::Sin($phase))
  $y = $baseY + [int](80 * [Math]::Cos($phase * 0.7))
  [Win32Smoke]::SetCursorPos($x, $y) | Out-Null
  $r = New-Object Win32Smoke+RECT
  [Win32Smoke]::GetWindowRect($target, [ref]$r) | Out-Null
  $dragSamples.Add([pscustomobject]@{ phase='drag_60s'; t_ms=$t; left=$r.Left; top=$r.Top; w=$r.Right-$r.Left; h=$r.Bottom-$r.Top })
  Start-Sleep -Milliseconds 50
}
[Win32Smoke]::mouse_event([Win32Smoke]::MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
$report.drag_60s = Analyze-MotionSamples -Samples $dragSamples -Label 'drag_60s'
foreach ($s in $dragSamples) { $motionLog.Add($s) }

Write-Host "Post-release 5s..."
$postRelease = @()
$sw2 = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw2.ElapsedMilliseconds -lt 5000) {
  $r = New-Object Win32Smoke+RECT
  [Win32Smoke]::GetWindowRect($target, [ref]$r) | Out-Null
  $postRelease += [pscustomobject]@{ phase='post_release'; t_ms=$sw2.ElapsedMilliseconds; left=$r.Left; top=$r.Top; w=$r.Right-$r.Left; h=$r.Bottom-$r.Top }
  Start-Sleep -Milliseconds 50
}
$report.post_release_5s = Analyze-MotionSamples -Samples $postRelease -Label 'post_release'
$report.post_release_drift_px = [Math]::Max([Math]::Abs($postRelease[-1].left-$postRelease[0].left), [Math]::Abs($postRelease[-1].top-$postRelease[0].top))
foreach ($s in $postRelease) { $motionLog.Add($s) }

$w2 = ([Win32Smoke]::FindSurfaceWindows([uint32]$proc.Id))[-1]
$report.resize = @{ before=[Win32Smoke]::RectStr($w2); after='' }
Resize-BottomRight -Hwnd $w2 -DeltaW 80 -DeltaH 60
$report.resize.after = [Win32Smoke]::RectStr($w2)

Write-Host "Quick back-forth..."
$quickSamples = New-Object System.Collections.Generic.List[object]
for ($n = 0; $n -lt 6; $n++) {
  $off = if ($n % 2 -eq 0) { 140 } else { -140 }
  Run-TitleDragOffset -Hwnd $target -DurationMs 280 -OffsetX $off
  $qs = Sample-WindowMotion -Hwnd $target -DurationMs 200 -IntervalMs 40
  foreach ($s in $qs) { $quickSamples.Add($s) }
}
$report.quick_moves = Analyze-MotionSamples -Samples $quickSamples -Label 'quick_moves'

$report.pre_exit_windows = @([Win32Smoke]::FindSurfaceWindows([uint32]$proc.Id) | ForEach-Object { [Win32Smoke]::RectStr($_) })

Write-Host "Graceful stop..."
Stop-DemoGracefully -Proc $proc
$report.exit_code = $proc.ExitCode

Write-Host "Restart..."
$proc2 = Start-Process -FilePath $DemoExe -ArgumentList @('--state-dir', $StateDir) -PassThru
$windows2 = Wait-SurfaceWindows -Proc $proc2 -MinCount 2
$report.restart_window_count = $windows2.Count
$report.restart_geometry = @($windows2 | ForEach-Object { [Win32Smoke]::RectStr($_) })
Start-Sleep -Seconds 2
Stop-DemoGracefully -Proc $proc2
$report.restart_exit_code = $proc2.ExitCode

Write-Host "load_only..."
$proc3 = Start-Process -FilePath $LoadOnlyExe -ArgumentList @('--state-dir', $StateDir) -PassThru
$windows3 = Wait-SurfaceWindows -Proc $proc3 -MinCount 2
$report.load_only_window_count = $windows3.Count
$report.load_only_geometry = @($windows3 | ForEach-Object { [Win32Smoke]::RectStr($_) })
Stop-DemoGracefully -Proc $proc3
$report.load_only_exit_code = $proc3.ExitCode

$csvPath = Join-Path $ArtifactDir 'motion_samples.csv'
$motionLog | Export-Csv -NoTypeInformation -Path $csvPath
$jsonPath = Join-Path $ArtifactDir 'smoke_report.json'
($report | ConvertTo-Json -Depth 6) | Set-Content -Encoding utf8 $jsonPath
Write-Host "Wrote $jsonPath"
