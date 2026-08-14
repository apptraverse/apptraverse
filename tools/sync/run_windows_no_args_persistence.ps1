# Windows no-argument load-or-build persistence smoke.
# Runs win32_single_client_chat.exe with NO CLI args from a temp cwd so state/ is local.

[CmdletBinding()]
param(
  [string]$ExePath = "",
  [string]$BuildDir = "",
  [int]$ReadyTimeoutSec = 180
)

$ErrorActionPreference = "Stop"
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Resolve-RepoRoot {
  $script_dir = Split-Path -Parent $PSCommandPath
  return (Resolve-Path (Join-Path $script_dir "..\..")).Path
}

function Write-Utf8NoBom([string]$Path, [string]$Text) {
  [System.IO.File]::WriteAllText($Path, $Text, $Utf8NoBom)
}

function Resolve-WinExe([string]$RepoRoot, [string]$BuildDir, [string]$ExePath) {
  if ($ExePath) {
    if (-not (Test-Path $ExePath)) { throw "ExePath not found: $ExePath" }
    return (Resolve-Path $ExePath).Path
  }
  $candidates = @()
  if ($BuildDir) {
    $candidates += (Join-Path $BuildDir "examples\single_client_chat\windows\Debug\win32_single_client_chat.exe")
  }
  $candidates += (Join-Path $RepoRoot "build-ogv2\examples\single_client_chat\windows\Debug\win32_single_client_chat.exe")
  $candidates += (Join-Path $RepoRoot "build-msvc\examples\single_client_chat\windows\Debug\win32_single_client_chat.exe")
  foreach ($c in $candidates) {
    if (Test-Path $c) { return (Resolve-Path $c).Path }
  }
  throw "win32_single_client_chat.exe not found. Pass -ExePath or build build-ogv2 Debug."
}

if (-not ("NoArgsWinClose" -as [type])) {
  Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class NoArgsWinClose {
  public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc lpEnumFunc, IntPtr lParam);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
  const uint WM_CLOSE = 0x0010;
  public static bool CloseProcessWindows(int pid) {
    bool posted = false;
    EnumWindows((hWnd, lParam) => {
      uint wpid;
      GetWindowThreadProcessId(hWnd, out wpid);
      if (wpid == (uint)pid) {
        PostMessage(hWnd, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
        posted = true;
      }
      return true;
    }, IntPtr.Zero);
    return posted;
  }
}
"@
}

function Start-NoArgsProcess([string]$Exe, [string]$WorkDir, [string]$LogPath) {
  Remove-Item $LogPath -ErrorAction SilentlyContinue
  $cmd = "cd /d `"$WorkDir`" && `"$Exe`" > `"$LogPath`" 2>&1"
  return Start-Process -FilePath "cmd.exe" -ArgumentList @("/c", $cmd) -PassThru -WindowStyle Hidden
}

function Get-NoArgsLogText([string]$LogPath) {
  if (-not (Test-Path $LogPath)) { return "" }
  $text = Get-Content -Raw -Path $LogPath -ErrorAction SilentlyContinue
  return $(if ($null -eq $text) { "" } else { $text })
}

function Wait-NoArgsMarker([System.Diagnostics.Process]$Process, [string]$LogPath, [string]$Pattern, [string]$Description, [int]$TimeoutSec) {
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $text = Get-NoArgsLogText $LogPath
    $match = ($text -split "`n" | Where-Object { $_ -match $Pattern } | Select-Object -Last 1)
    if ($match) {
      Write-Host "  OK  $Description"
      return $match.Trim()
    }
    if ($Process.HasExited) {
      throw "Process exited early (code=$($Process.ExitCode)) waiting for $Description`n$text"
    }
    Start-Sleep -Milliseconds 300
  }
  throw "Timed out waiting for $Description (pattern: $Pattern)`n$(Get-NoArgsLogText $LogPath)"
}

function Stop-NoArgsGraceful([System.Diagnostics.Process]$CmdProcess, [int]$TimeoutSec = 30) {
  $chat = Get-Process win32_single_client_chat -ErrorAction SilentlyContinue
  foreach ($p in $chat) {
    $posted = [NoArgsWinClose]::CloseProcessWindows($p.Id)
    Write-Host "  WM_CLOSE posted=$posted pid=$($p.Id)"
  }
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $alive = Get-Process win32_single_client_chat -ErrorAction SilentlyContinue
    if (-not $alive) { break }
    Start-Sleep -Milliseconds 200
  }
  Get-Process win32_single_client_chat -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
  if ($null -ne $CmdProcess -and -not $CmdProcess.HasExited) {
    try { $CmdProcess.WaitForExit(5000) | Out-Null } catch { $null = $_ }
  }
}

$repo_root = Resolve-RepoRoot
$exe = Resolve-WinExe $repo_root $BuildDir $ExePath
$out_dir = Join-Path $repo_root "build\no-args-persistence"
New-Item -ItemType Directory -Force -Path $out_dir | Out-Null
$work = Join-Path $out_dir ("run-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
New-Item -ItemType Directory -Force -Path $work | Out-Null

Write-Host "Exe                 : $exe"
Write-Host "Working directory   : $work"
Write-Host "NOTE: no --distill; state/ is created under the working directory"

$log1 = Join-Path $work "launch1.log"
Write-Host ""
Write-Host "Launch 1 (no args)"
$p1 = Start-NoArgsProcess $exe $work $log1
try {
  Wait-NoArgsMarker $p1 $log1 "WINDOWS_GRAPH_CREATED" "WINDOWS_GRAPH_CREATED" $ReadyTimeoutSec | Out-Null
  $ready1 = Wait-NoArgsMarker $p1 $log1 "AETHER_CLIENT_READY platform=windows uid=" "AETHER_CLIENT_READY" $ReadyTimeoutSec
  $app1 = Wait-NoArgsMarker $p1 $log1 "APP_CLIENT_READY platform=windows obj_id=" "APP_CLIENT_READY" $ReadyTimeoutSec
  if ($ready1 -notmatch "uid=([0-9a-fA-F-]+)") { throw "Unable to parse UID: $ready1" }
  $uid1 = $Matches[1]
  if ($app1 -notmatch "obj_id=(\d+)") { throw "Unable to parse obj_id: $app1" }
  $obj1 = $Matches[1]
  Write-Host "  UID=$uid1 obj_id=$obj1"
} catch {
  Stop-NoArgsGraceful $p1 5
  throw
}
Stop-NoArgsGraceful $p1 45
Start-Sleep -Seconds 1

if (-not (Test-Path (Join-Path $work "state"))) {
  throw "Expected state/ directory after first launch"
}

$log2 = Join-Path $work "launch2.log"
Write-Host ""
Write-Host "Launch 2 (same cwd, no args, no wipe)"
$p2 = Start-NoArgsProcess $exe $work $log2
try {
  Wait-NoArgsMarker $p2 $log2 "WINDOWS_GRAPH_LOADED" "WINDOWS_GRAPH_LOADED" $ReadyTimeoutSec | Out-Null
  $ready2 = Wait-NoArgsMarker $p2 $log2 "AETHER_CLIENT_READY platform=windows uid=" "AETHER_CLIENT_READY #2" $ReadyTimeoutSec
  $app2 = Wait-NoArgsMarker $p2 $log2 "APP_CLIENT_READY platform=windows obj_id=" "APP_CLIENT_READY #2" $ReadyTimeoutSec
  if ($ready2 -notmatch "uid=([0-9a-fA-F-]+)") { throw "Unable to parse UID #2: $ready2" }
  $uid2 = $Matches[1]
  if ($app2 -notmatch "obj_id=(\d+)") { throw "Unable to parse obj_id #2: $app2" }
  $obj2 = $Matches[1]
  if ($uid2 -ne $uid1) { throw "Aether UID changed: $uid1 -> $uid2" }
  if ($obj2 -ne $obj1) { throw "Client ObjId changed: $obj1 -> $obj2" }
  $text2 = Get-NoArgsLogText $log2
  if ($text2 -match "WINDOWS_GRAPH_CREATED") {
    throw "Second launch unexpectedly created a new graph"
  }
  Write-Host "  OK  same UID=$uid2 same obj_id=$obj2"
} finally {
  Stop-NoArgsGraceful $p2 45
}

$summary = @"
result=PASS
uid=$uid1
obj_id=$obj1
work_dir=$work
exe=$exe
"@
Write-Utf8NoBom (Join-Path $work "summary.txt") $summary
Write-Host ""
Write-Host "NO_ARGS_PERSISTENCE PASS"
exit 0
