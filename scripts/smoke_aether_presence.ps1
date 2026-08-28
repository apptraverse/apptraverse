param(
  [string]$Exe = "build\win64-ninja-msvc-release\examples\chat_ui_runtime_demo\windows\win32_chat_ui_runtime_demo.exe",
  [string]$StateDir = "build\smoke_presence_state",
  [int]$HoldSec = 30
)

$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Close {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
  public const uint WM_CLOSE = 0x0010;
}
"@

function Wait-LogLine {
  param([string]$Path, [string]$Pattern, [int]$TimeoutSec = 90)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if (Test-Path $Path) {
      $text = Get-Content $Path -Raw
      if ($text -match $Pattern) { return $true }
    }
    Start-Sleep -Milliseconds 500
  }
  return $false
}

function Stop-Gracefully {
  param([System.Diagnostics.Process]$Proc, [int]$TimeoutSec = 20)
  if ($Proc.HasExited) { return }
  $hwnd = $Proc.MainWindowHandle
  if ($hwnd -ne [IntPtr]::Zero) {
    [void][Win32Close]::PostMessage($hwnd, [Win32Close]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
  } else {
    $Proc.CloseMainWindow() | Out-Null
  }
  if (-not $Proc.WaitForExit($TimeoutSec * 1000)) {
    throw "Process did not exit gracefully"
  }
}

$absExe = (Resolve-Path $Exe).Path
$absState = Join-Path (Resolve-Path .).Path $StateDir
$logPath = Join-Path (Resolve-Path .).Path "build\smoke_presence.log"

if (Test-Path $StateDir) { Remove-Item -Recurse -Force $StateDir }
if (Test-Path $logPath) { Remove-Item -Force $logPath }

Write-Host "Distilling"
$distill = Start-Process -FilePath $absExe -ArgumentList @("--distill", $absState) -PassThru -Wait
if ($distill.ExitCode -ne 0) { throw "Distill failed" }

Write-Host "Running ${HoldSec}s presence smoke"
$proc = Start-Process -FilePath $absExe -ArgumentList @("--state-dir", $absState) `
  -RedirectStandardOutput $logPath -PassThru

if (-not (Wait-LogLine -Path $logPath -Pattern "AETHER_RX_SCHEDULE_SET")) {
  throw "missing RX schedule log"
}
if (-not (Wait-LogLine -Path $logPath -Pattern "LOCAL_PRESENCE state=online")) {
  throw "missing LOCAL_PRESENCE online within timeout"
}

Start-Sleep -Seconds $HoldSec
$log = Get-Content $logPath -Raw
$onlineCount = ([regex]::Matches($log, "LOCAL_PRESENCE state=online")).Count
$offlineCount = ([regex]::Matches($log, "LOCAL_PRESENCE state=offline")).Count
Write-Host "presence transitions: online=$onlineCount offline=$offlineCount"

Stop-Gracefully -Proc $proc
if ($onlineCount -lt 1) { throw "expected at least one online transition" }
Write-Host "PASS: presence smoke"
