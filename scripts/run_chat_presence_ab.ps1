#requires -Version 5.1
<#
.SYNOPSIS
  Local-only Presence A/B validation for win32_chat_ui_runtime_demo.

.DESCRIPTION
  Distills two independent state dirs, acquires persistent Aether UIDs, then
  launches Host/Client without peer monitoring. Asserts LOCAL_PRESENCE online
  with session_id for each process, including after stop/restart. Exits
  non-zero on failure.
#>
param(
  [string]$BuildDir = "",
  [string]$WorkRoot = "",
  [int]$OnlineTimeoutSec = 90,
  [int]$UidTimeoutSec = 90
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $BuildDir) {
  $BuildDir = Join-Path $RepoRoot "build\win64-ninja-msvc-debug"
}
if (-not $WorkRoot) {
  $WorkRoot = Join-Path $RepoRoot ".artifacts\chat-presence-ab"
}

$Exe = Join-Path $BuildDir "examples\chat_ui_runtime_demo\windows\win32_chat_ui_runtime_demo.exe"
if (-not (Test-Path $Exe)) {
  Write-Error "Missing chat executable: $Exe"
}

function Write-Step([string]$msg) {
  Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $msg)
}

function Stop-ChatProcesses {
  Get-CimInstance Win32_Process -Filter "Name = 'win32_chat_ui_runtime_demo.exe'" -ErrorAction SilentlyContinue |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

function Reset-Dir([string]$path) {
  if (Test-Path $path) {
    Remove-Item -Recurse -Force $path
  }
  New-Item -ItemType Directory -Force -Path $path | Out-Null
}

function Get-LogPath([string]$stateDir) {
  Join-Path $stateDir "chat_runtime.log"
}

function Get-UidPath([string]$stateDir) {
  Join-Path $stateDir "aether\last_uid.txt"
}

function Get-Uid([string]$stateDir) {
  $p = Get-UidPath $stateDir
  if (-not (Test-Path $p)) { return $null }
  $raw = (Get-Content $p -Raw -ErrorAction SilentlyContinue)
  if (-not $raw) { return $null }
  return $raw.Trim()
}

function Get-LogLines([string]$logPath) {
  if (-not (Test-Path $logPath)) { return @() }
  return @(Get-Content -Path $logPath -ErrorAction SilentlyContinue)
}

function Get-MatchLineIndex([string[]]$lines, [string]$pattern, [int]$afterIndex = -1) {
  $rx = [regex]$pattern
  for ($i = $afterIndex + 1; $i -lt $lines.Count; $i++) {
    if ($rx.IsMatch($lines[$i])) {
      return $i
    }
  }
  return -1
}

function Wait-LogMatch(
  [string]$logPath,
  [string]$pattern,
  [int]$timeoutSec,
  [string]$label,
  [int]$afterIndex = -1
) {
  $deadline = (Get-Date).AddSeconds($timeoutSec)
  while ((Get-Date) -lt $deadline) {
    $lines = Get-LogLines $logPath
    $idx = Get-MatchLineIndex $lines $pattern $afterIndex
    if ($idx -ge 0) {
      Write-Step ("OK {0}: {1}" -f $label, $lines[$idx])
      return [pscustomobject]@{
        Line  = $lines[$idx]
        Index = $idx
      }
    }
    Start-Sleep -Milliseconds 250
  }
  throw ("TIMEOUT waiting for '{0}' in {1} afterIndex={2} ({3}s)" -f `
      $pattern, $logPath, $afterIndex, $timeoutSec)
}

function Extract-SessionId([string]$line) {
  if ($line -match 'session_id=(\S+)') {
    return $Matches[1]
  }
  return $null
}

function Wait-AppSession([string]$logPath, [int]$timeoutSec, [string]$label, [int]$afterIndex = -1) {
  $hit = Wait-LogMatch $logPath "APP_SESSION_START session_id=" $timeoutSec $label $afterIndex
  $sid = Extract-SessionId $hit.Line
  if (-not $sid) {
    throw ("Failed to parse session_id from: {0}" -f $hit.Line)
  }
  Write-Step ("{0} session_id={1}" -f $label, $sid)
  return [pscustomobject]@{
    SessionId = $sid
    Index     = $hit.Index
    Line      = $hit.Line
  }
}

function Wait-UidFile([string]$stateDir, [int]$timeoutSec, [string]$label) {
  $deadline = (Get-Date).AddSeconds($timeoutSec)
  while ((Get-Date) -lt $deadline) {
    $uid = Get-Uid $stateDir
    if ($uid) {
      Write-Step ("OK {0} uid={1}" -f $label, $uid)
      return $uid
    }
    Start-Sleep -Milliseconds 200
  }
  throw ("TIMEOUT waiting for last_uid.txt under {0} ({1}s)" -f $stateDir, $timeoutSec)
}

function Wait-LocalOnline(
  [string]$logPath,
  [string]$sessionId,
  [int]$timeoutSec,
  [string]$label,
  [int]$afterIndex = -1
) {
  $pattern = "LOCAL_PRESENCE state=online session_id=$([regex]::Escape($sessionId))"
  return Wait-LogMatch $logPath $pattern $timeoutSec ("$label local ONLINE") $afterIndex
}

function Assert-LatestLocalOnline([string]$logPath, [string]$sessionId, [string]$label) {
  $pattern = "LOCAL_PRESENCE state=.*session_id=$([regex]::Escape($sessionId))"
  $lines = Get-LogLines $logPath
  $rx = [regex]$pattern
  $last = $null
  for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($rx.IsMatch($lines[$i])) {
      $last = $lines[$i]
    }
  }
  if (-not $last) {
    throw ("ASSERT {0}: no LOCAL_PRESENCE for session_id={1}" -f $label, $sessionId)
  }
  if ($last -notmatch "state=online") {
    throw ("ASSERT {0}: latest LOCAL_PRESENCE not online: {1}" -f $label, $last)
  }
  Write-Step ("OK {0} still local ONLINE: {1}" -f $label, $last)
}

function Assert-NoRemotePresence([string]$logPath, [string]$label) {
  $hits = @(Select-String -Path $logPath -Pattern "REMOTE_PRESENCE" -AllMatches -ErrorAction SilentlyContinue)
  if ($hits.Count -gt 0) {
    throw ("ASSERT {0}: unexpected REMOTE_PRESENCE lines in {1}" -f $label, $logPath)
  }
  Write-Step ("OK {0}: no REMOTE_PRESENCE in log" -f $label)
}

function Start-Chat(
  [string]$role,
  [string]$stateDir
) {
  $args = @("--$role", "--state-dir", $stateDir)
  Write-Step ("Launch {0}: {1}" -f $role, $stateDir)
  return Start-Process -FilePath $Exe -ArgumentList $args -PassThru -WindowStyle Normal
}

function Stop-Pid([System.Diagnostics.Process]$proc, [string]$label) {
  if ($null -eq $proc) { return }
  Write-Step ("Stop {0} pid={1}" -f $label, $proc.Id)
  if (-not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
  }
  $proc.WaitForExit(10000) | Out-Null
}

function Get-LogLineCount([string]$logPath) {
  return (Get-LogLines $logPath).Count
}

function Acquire-Uid(
  [string]$role,
  [string]$stateDir,
  [string]$label
) {
  $before = Get-LogLineCount (Get-LogPath $stateDir)
  $proc = Start-Chat $role $stateDir
  try {
    $session = Wait-AppSession (Get-LogPath $stateDir) $OnlineTimeoutSec `
        "$label acquire session" ($before - 1)
    Wait-LocalOnline (Get-LogPath $stateDir) $session.SessionId $OnlineTimeoutSec $label | Out-Null
    $uid = Wait-UidFile $stateDir $UidTimeoutSec $label
    return $uid
  }
  finally {
    Stop-Pid $proc $label
  }
}

# --- main ---
$procA = $null
$procB = $null
$A = Join-Path $WorkRoot "A"
$B = Join-Path $WorkRoot "B"

try {
  Stop-ChatProcesses
  Reset-Dir $WorkRoot
  $A = Join-Path $WorkRoot "A"
  $B = Join-Path $WorkRoot "B"
  Reset-Dir $A
  Reset-Dir $B

  Write-Step "Distill A (Host) / B (Client)"
  & $Exe --distill --state-dir $A --name Host | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "Distill A failed with exit $LASTEXITCODE" }
  & $Exe --distill --state-dir $B --name Client | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "Distill B failed with exit $LASTEXITCODE" }

  Write-Step "Acquire persistent UID A (brief host launch)"
  $uidA = Acquire-Uid "host" $A "A"
  Write-Step "Acquire persistent UID B (brief client launch)"
  $uidB = Acquire-Uid "client" $B "B"
  if ($uidA -eq $uidB) {
    throw ("ASSERT UIDs must differ: A={0} B={1}" -f $uidA, $uidB)
  }
  Write-Step ("UIDs ready A={0} B={1}" -f $uidA, $uidB)

  $beforeA = Get-LogLineCount (Get-LogPath $A)
  $procA = Start-Chat "host" $A
  $sessionA = Wait-AppSession (Get-LogPath $A) $OnlineTimeoutSec "A session" ($beforeA - 1)
  $sa = $sessionA.SessionId
  Wait-LocalOnline (Get-LogPath $A) $sa $OnlineTimeoutSec "A" | Out-Null

  $beforeB = Get-LogLineCount (Get-LogPath $B)
  $procB = Start-Chat "client" $B
  $sessionB = Wait-AppSession (Get-LogPath $B) $OnlineTimeoutSec "B session" ($beforeB - 1)
  $sb = $sessionB.SessionId
  Wait-LocalOnline (Get-LogPath $B) $sb $OnlineTimeoutSec "B" | Out-Null

  Assert-LatestLocalOnline (Get-LogPath $A) $sa "A concurrent"
  Assert-LatestLocalOnline (Get-LogPath $B) $sb "B concurrent"

  Write-Step "Restart A (same state dir); expect fresh local ONLINE"
  Stop-Pid $procA "A"
  $procA = $null
  $beforeA2 = Get-LogLineCount (Get-LogPath $A)
  $procA = Start-Chat "host" $A
  $sessionA2 = Wait-AppSession (Get-LogPath $A) $OnlineTimeoutSec "A restart session" ($beforeA2 - 1)
  $sa2 = $sessionA2.SessionId
  Wait-LocalOnline (Get-LogPath $A) $sa2 $OnlineTimeoutSec "A restarted" | Out-Null
  Assert-LatestLocalOnline (Get-LogPath $A) $sa2 "A after restart"

  Write-Step "Restart B (same state dir); expect fresh local ONLINE"
  Stop-Pid $procB "B"
  $procB = $null
  $beforeB2 = Get-LogLineCount (Get-LogPath $B)
  $procB = Start-Chat "client" $B
  $sessionB2 = Wait-AppSession (Get-LogPath $B) $OnlineTimeoutSec "B restart session" ($beforeB2 - 1)
  $sb2 = $sessionB2.SessionId
  Wait-LocalOnline (Get-LogPath $B) $sb2 $OnlineTimeoutSec "B restarted" | Out-Null
  Assert-LatestLocalOnline (Get-LogPath $B) $sb2 "B after restart"

  Assert-NoRemotePresence (Get-LogPath $A) "A log"
  Assert-NoRemotePresence (Get-LogPath $B) "B log"

  Write-Step "PASS local-only Presence A/B validation"
  Write-Host ("SESSIONS sa={0} sb={1} sa2={2} sb2={3}" -f $sa, $sb, $sa2, $sb2)
  exit 0
}
catch {
  Write-Host ("FAIL: {0}" -f $_.Exception.Message) -ForegroundColor Red
  if (Test-Path (Get-LogPath $A)) {
    Write-Host "---- A log (tail) ----"
    Get-Content (Get-LogPath $A) -Tail 60
  }
  if (Test-Path (Get-LogPath $B)) {
    Write-Host "---- B log (tail) ----"
    Get-Content (Get-LogPath $B) -Tail 60
  }
  exit 1
}
finally {
  Stop-Pid $procA "A"
  Stop-Pid $procB "B"
}
