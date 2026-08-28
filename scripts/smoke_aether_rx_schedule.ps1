param(
  [string]$Exe = "build\win64-ninja-msvc-release\examples\chat_ui_runtime_demo\windows\win32_chat_ui_runtime_demo.exe",
  [string]$StateDir = "build\smoke_rx_schedule_state"
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

function Wait-UidFile {
  param([string]$Path, [int]$TimeoutSec = 90)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if (Test-Path $Path) {
      $text = (Get-Content $Path -Raw).Trim()
      if ($text -and $text -ne "...") { return $text }
    }
    Start-Sleep -Milliseconds 500
  }
  throw "Timed out waiting for UID at $Path"
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
    throw "Process did not exit gracefully (pid=$($Proc.Id))"
  }
}

function Run-App {
  param([string]$State, [string]$LogPath, [int]$HoldSec = 0)
  $absExe = (Resolve-Path $Exe).Path
  $absState = Join-Path (Resolve-Path .).Path $State
  $absLog = Join-Path (Resolve-Path .).Path $LogPath
  $proc = Start-Process -FilePath $absExe -ArgumentList @("--state-dir", $absState) `
    -RedirectStandardOutput $absLog -PassThru
  $uidPath = Join-Path $State "aether\last_uid.txt"
  $uid = Wait-UidFile -Path $uidPath
  if ($HoldSec -gt 0) { Start-Sleep -Seconds $HoldSec }
  Stop-Gracefully -Proc $proc
  return @{ Uid = $uid; Log = (Get-Content $absLog -Raw) }
}

$absExe = (Resolve-Path $Exe).Path
$absState = Join-Path (Resolve-Path .).Path $StateDir

if (Test-Path $StateDir) { Remove-Item -Recurse -Force $StateDir }
Write-Host "Distilling fresh state"
$distill = Start-Process -FilePath $absExe -ArgumentList @("--distill", $absState) -PassThru -Wait
if ($distill.ExitCode -ne 0) { throw "Distill failed" }

Write-Host "Fresh run (register + schedule)"
$r1 = Run-App -State $StateDir -LogPath "build\smoke_rx_fresh.log" -HoldSec 15
if ($r1.Log -notmatch "AETHER_CLIENT_READY uid=") { throw "missing AETHER_CLIENT_READY" }
if ($r1.Log -notmatch "AETHER_RX_SCHEDULE_SET ping_ms=3000 window_ms=3000") {
  throw "missing AETHER_RX_SCHEDULE_SET"
}
Write-Host "uid1=$($r1.Uid)"

Write-Host "Restart run (existing client + schedule)"
$r2 = Run-App -State $StateDir -LogPath "build\smoke_rx_restart.log" -HoldSec 2
if ($r2.Uid -ne $r1.Uid) { throw "UID mismatch: $($r1.Uid) vs $($r2.Uid)" }
if ($r2.Log -notmatch "AETHER_RX_SCHEDULE_SET ping_ms=3000 window_ms=3000") {
  throw "schedule not set on restart"
}
Write-Host "uid2=$($r2.Uid)"
Write-Host "PASS: RX schedule smoke"
