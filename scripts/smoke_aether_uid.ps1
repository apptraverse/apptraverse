param(
  [string]$Exe = "build\win64-ninja-msvc-release\examples\chat_ui_runtime_demo\windows\win32_chat_ui_runtime_demo.exe",
  [string]$StateDir = "build\smoke_aether_state"
)

$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Close {
  [DllImport("user32.dll")]
  public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
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
    throw "Process did not exit gracefully"
  }
}

if (Test-Path $StateDir) {
  Remove-Item -Recurse -Force $StateDir
}

$absExe = (Resolve-Path $Exe).Path
$absState = Join-Path (Resolve-Path .).Path $StateDir

Write-Host "Distilling chat state"
$distill = Start-Process -FilePath $absExe -ArgumentList @("--distill", $absState) -PassThru -Wait
if ($distill.ExitCode -ne 0) {
  throw "Distill failed with exit code $($distill.ExitCode)"
}

$uidPath = Join-Path $StateDir "aether\last_uid.txt"

Write-Host "Run #1 (fresh state)"
$p1 = Start-Process -FilePath $absExe -ArgumentList @("--state-dir", $absState) -PassThru
$uid1 = Wait-UidFile -Path $uidPath
Write-Host "UID1: $uid1"
Stop-Gracefully -Proc $p1

Write-Host "Run #2 (existing client)"
$p2 = Start-Process -FilePath $absExe -ArgumentList @("--state-dir", $absState) -PassThru
$uid2 = Wait-UidFile -Path $uidPath
Write-Host "UID2: $uid2"
Stop-Gracefully -Proc $p2

if ($uid1 -ne $uid2) {
  throw "UID mismatch: '$uid1' vs '$uid2'"
}

Write-Host "PASS: persistent UID across restart"
