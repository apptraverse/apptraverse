#requires -Version 5.1
<#
.SYNOPSIS
  Presence-only A/B validation for win32_chat_ui_runtime_demo.

.DESCRIPTION
  Distills two independent state dirs, acquires persistent Aether UIDs, then
  launches Host/Client with --monitor-peer-uid (AddPresencePeer path only).
  Asserts local/remote Presence transitions across stop/restart without any
  Shared P2P / journal transport. Exits non-zero on failure.
#>
param(
  [string]$BuildDir = "",
  [string]$WorkRoot = "",
  [int]$OnlineTimeoutSec = 90,
  [int]$OfflineTimeoutSec = 45,
  [int]$ConnectTimeoutSec = 90,
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

$script:ForbiddenSharedPatterns = @(
  "SHARED_STREAM_OPENING",
  "SHARED_P2P_WRITE",
  "SHARED_EVENT_SEND",
  "SHARED_EVENT_PENDING"
)

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

function Select-LogMatches([string]$logPath, [string]$pattern) {
  if (-not (Test-Path $logPath)) { return @() }
  return @(Select-String -Path $logPath -Pattern $pattern -AllMatches |
    ForEach-Object { $_.Line })
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
  [string]$label
) {
  $pattern = "LOCAL_PRESENCE state=online session_id=$([regex]::Escape($sessionId))"
  return Wait-LogMatch $logPath $pattern $timeoutSec ("$label local ONLINE")
}

function Wait-RemoteState(
  [string]$logPath,
  [string]$sessionId,
  [string]$peerUid,
  [string]$state,
  [int]$timeoutSec,
  [string]$label,
  [int]$afterIndex = -1
) {
  $pattern = ("REMOTE_PRESENCE peer={0} state={1} session_id={2}" -f `
      [regex]::Escape($peerUid), [regex]::Escape($state), [regex]::Escape($sessionId))
  return Wait-LogMatch $logPath $pattern $timeoutSec $label $afterIndex
}

function Get-LatestRemotePresence(
  [string]$logPath,
  [string]$sessionId,
  [string]$peerUid
) {
  $pattern = ("REMOTE_PRESENCE peer={0} state=(\S+) session_id={1}" -f `
      [regex]::Escape($peerUid), [regex]::Escape($sessionId))
  $lines = Get-LogLines $logPath
  $last = $null
  $idx = -1
  $rx = [regex]$pattern
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $m = $rx.Match($lines[$i])
    if ($m.Success) {
      $last = $m.Groups[1].Value
      $idx = $i
    }
  }
  return [pscustomobject]@{
    State = $last
    Index = $idx
    Line  = $(if ($idx -ge 0) { $lines[$idx] } else { $null })
  }
}

# Wait until the *latest* REMOTE_PRESENCE for peer/session equals $state.
# Returns that line index so a later OFFLINE wait can require a newer transition.
function Wait-LatestRemoteState(
  [string]$logPath,
  [string]$sessionId,
  [string]$peerUid,
  [string]$state,
  [int]$timeoutSec,
  [string]$label
) {
  $deadline = (Get-Date).AddSeconds($timeoutSec)
  while ((Get-Date) -lt $deadline) {
    $cur = Get-LatestRemotePresence $logPath $sessionId $peerUid
    if ($cur.State -eq $state) {
      Write-Step ("OK {0}: {1}" -f $label, $cur.Line)
      return $cur
    }
    Start-Sleep -Milliseconds 250
  }
  $cur = Get-LatestRemotePresence $logPath $sessionId $peerUid
  throw ("TIMEOUT waiting for latest REMOTE_PRESENCE peer={0} state={1} session={2} (last={3}) ({4}s)" -f `
      $peerUid, $state, $sessionId, $cur.State, $timeoutSec)
}

function Wait-UiContact(
  [string]$logPath,
  [string]$sessionId,
  [string]$uid,
  [string]$state,
  [int]$timeoutSec,
  [string]$label,
  [int]$afterIndex = -1
) {
  $pattern = ("UI_PRESENCE contacts=.*{0}:{1}.*session_id={2}" -f `
      [regex]::Escape($uid), [regex]::Escape($state), [regex]::Escape($sessionId))
  return Wait-LogMatch $logPath $pattern $timeoutSec $label $afterIndex
}

function Assert-LatestLocalOnline([string]$logPath, [string]$sessionId, [string]$label) {
  $pattern = "LOCAL_PRESENCE state=.*session_id=$([regex]::Escape($sessionId))"
  $hits = Select-LogMatches $logPath $pattern
  if ($hits.Count -eq 0) {
    throw ("ASSERT {0}: no LOCAL_PRESENCE for session_id={1}" -f $label, $sessionId)
  }
  $last = $hits[-1]
  if ($last -notmatch "state=online") {
    throw ("ASSERT {0}: latest LOCAL_PRESENCE not online: {1}" -f $label, $last)
  }
  Write-Step ("OK {0} still local ONLINE: {1}" -f $label, $last)
}

function Assert-NoSharedTransportInSession(
  [string]$logPath,
  [string]$sessionId,
  [string]$label
) {
  $lines = Get-LogLines $logPath
  $startIdx = Get-MatchLineIndex $lines ("APP_SESSION_START session_id=$([regex]::Escape($sessionId))")
  if ($startIdx -lt 0) {
    throw ("ASSERT {0}: missing APP_SESSION_START session_id={1}" -f $label, $sessionId)
  }
  $endIdx = $lines.Count - 1
  for ($i = $startIdx + 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match "APP_SESSION_START session_id=") {
      $endIdx = $i - 1
      break
    }
  }
  for ($i = $startIdx; $i -le $endIdx; $i++) {
    foreach ($bad in $script:ForbiddenSharedPatterns) {
      if ($lines[$i] -like "*$bad*") {
        throw ("ASSERT {0}: forbidden '{1}' in session_id={2}: {3}" -f `
            $label, $bad, $sessionId, $lines[$i])
      }
    }
  }
  Write-Step ("OK {0}: no Shared transport lines in session_id={1}" -f $label, $sessionId)
}

function Start-Chat(
  [string]$role,
  [string]$stateDir,
  [string]$monitorPeerUid = ""
) {
  $args = @("--$role", "--state-dir", $stateDir)
  if ($monitorPeerUid) {
    $args += @("--monitor-peer-uid", $monitorPeerUid)
  }
  Write-Step ("Launch {0}: {1} monitor={2}" -f $role, $stateDir, `
      $(if ($monitorPeerUid) { $monitorPeerUid } else { "<none>" }))
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
$dtOfflineA = -1
$dtOfflineB = -1
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

  # --- mutual monitor (Presence-only; no --connect-host-uid) ---
  $beforeA = Get-LogLineCount (Get-LogPath $A)
  $procA = Start-Chat "host" $A $uidB
  $sessionA = Wait-AppSession (Get-LogPath $A) $OnlineTimeoutSec "A monitor session" ($beforeA - 1)
  $sa2 = $sessionA.SessionId
  Wait-LocalOnline (Get-LogPath $A) $sa2 $OnlineTimeoutSec "A" | Out-Null
  $aSeesBUnknown = Wait-RemoteState (Get-LogPath $A) $sa2 $uidB "unknown" $ConnectTimeoutSec `
      "A sees B UNKNOWN (before peer up)"
  # B is not up yet; ONLINE for B comes after B launches.

  $beforeB = Get-LogLineCount (Get-LogPath $B)
  $procB = Start-Chat "client" $B $uidA
  $sessionB = Wait-AppSession (Get-LogPath $B) $OnlineTimeoutSec "B monitor session" ($beforeB - 1)
  $sb2 = $sessionB.SessionId
  Wait-LocalOnline (Get-LogPath $B) $sb2 $OnlineTimeoutSec "B" | Out-Null

  Wait-RemoteState (Get-LogPath $A) $sa2 $uidB "online" $ConnectTimeoutSec `
      "A sees B ONLINE" $aSeesBUnknown.Index | Out-Null
  Wait-RemoteState (Get-LogPath $B) $sb2 $uidA "online" $ConnectTimeoutSec `
      "B sees A ONLINE" | Out-Null
  # Re-confirm latest is ONLINE immediately before stop so a cloud flap OFFLINE
  # cannot starve the post-stop OFFLINE assertion via dedup.
  $bOnlineBeforeStopA = Wait-LatestRemoteState (Get-LogPath $B) $sb2 $uidA "online" `
      $ConnectTimeoutSec "B latest remote A ONLINE before stop A"
  Wait-LatestRemoteState (Get-LogPath $A) $sa2 $uidB "online" `
      $ConnectTimeoutSec "A latest remote B ONLINE before stop A" | Out-Null
  Wait-UiContact (Get-LogPath $A) $sa2 $uidB "online" $ConnectTimeoutSec `
      "A UI remote B ONLINE" | Out-Null
  Wait-UiContact (Get-LogPath $B) $sb2 $uidA "online" $ConnectTimeoutSec `
      "B UI remote A ONLINE" | Out-Null

  Assert-NoSharedTransportInSession (Get-LogPath $A) $sa2 "A after Add/monitor"
  Assert-NoSharedTransportInSession (Get-LogPath $B) $sb2 "B after Add/monitor"

  # --- Stop A; B stays in Sb2 and sees A OFFLINE ---
  Write-Step "Stop A forcefully; B session stays; wait B sees A OFFLINE"
  $t0 = Get-Date
  Stop-Pid $procA "A"
  $procA = $null
  $bSeesAOffline = Wait-RemoteState (Get-LogPath $B) $sb2 $uidA "offline" $OfflineTimeoutSec `
      "B sees A OFFLINE (new after stop)" $bOnlineBeforeStopA.Index
  Assert-LatestLocalOnline (Get-LogPath $B) $sb2 "B after A stop"
  $dtOfflineA = [int]((Get-Date) - $t0).TotalSeconds
  Write-Step ("Timing: A->OFFLINE observed by B in {0}s" -f $dtOfflineA)

  # --- Restart A monitoring B; B must see a fresh ONLINE after the OFFLINE ---
  Write-Step "Restart A --host --monitor-peer-uid uidB (same state dir)"
  $beforeA3 = Get-LogLineCount (Get-LogPath $A)
  $procA = Start-Chat "host" $A $uidB
  $sessionA3 = Wait-AppSession (Get-LogPath $A) $OnlineTimeoutSec "A restart session" ($beforeA3 - 1)
  $sa3 = $sessionA3.SessionId
  Wait-LocalOnline (Get-LogPath $A) $sa3 $OnlineTimeoutSec "A restarted" | Out-Null
  Wait-RemoteState (Get-LogPath $B) $sb2 $uidA "online" $ConnectTimeoutSec `
      "B sees A ONLINE again (after OFFLINE)" $bSeesAOffline.Index | Out-Null
  Wait-UiContact (Get-LogPath $B) $sb2 $uidA "online" $ConnectTimeoutSec `
      "B UI A ONLINE again" $bSeesAOffline.Index | Out-Null
  # A must also observe B ONLINE in the restarted session before we stop B,
  # otherwise a pre-stop OFFLINE sample can satisfy (and then starve) the
  # post-stop OFFLINE assertion via Aether-side dedup.
  $aSeesBOnlineAgain = Wait-LatestRemoteState (Get-LogPath $A) $sa3 $uidB "online" `
      $ConnectTimeoutSec "A restarted latest remote B ONLINE"
  Wait-UiContact (Get-LogPath $A) $sa3 $uidB "online" $ConnectTimeoutSec `
      "A UI B ONLINE after A restart" $sessionA3.Index | Out-Null
  Assert-NoSharedTransportInSession (Get-LogPath $A) $sa3 "A restart after Add/monitor"

  # --- Stop B; A sees B OFFLINE ---
  Write-Step "Stop B; A must stay local ONLINE and see B OFFLINE"
  $t1 = Get-Date
  Stop-Pid $procB "B"
  $procB = $null
  $aSeesBOffline = Wait-RemoteState (Get-LogPath $A) $sa3 $uidB "offline" $OfflineTimeoutSec `
      "A sees B OFFLINE (new after stop)" $aSeesBOnlineAgain.Index
  Assert-LatestLocalOnline (Get-LogPath $A) $sa3 "A after B stop"
  $dtOfflineB = [int]((Get-Date) - $t1).TotalSeconds
  Write-Step ("Timing: B->OFFLINE observed by A in {0}s" -f $dtOfflineB)

  # --- Restart B monitoring A; A sees B ONLINE again ---
  Write-Step "Restart B --client --monitor-peer-uid uidA (same state dir)"
  $beforeB3 = Get-LogLineCount (Get-LogPath $B)
  $procB = Start-Chat "client" $B $uidA
  $sessionB3 = Wait-AppSession (Get-LogPath $B) $OnlineTimeoutSec "B restart session" ($beforeB3 - 1)
  $sb3 = $sessionB3.SessionId
  Wait-LocalOnline (Get-LogPath $B) $sb3 $OnlineTimeoutSec "B restarted" | Out-Null
  Wait-RemoteState (Get-LogPath $A) $sa3 $uidB "online" $ConnectTimeoutSec `
      "A sees B ONLINE again (after OFFLINE)" $aSeesBOffline.Index | Out-Null
  Wait-UiContact (Get-LogPath $A) $sa3 $uidB "online" $ConnectTimeoutSec `
      "A UI B ONLINE again" $aSeesBOffline.Index | Out-Null
  Assert-NoSharedTransportInSession (Get-LogPath $B) $sb3 "B restart after Add/monitor"

  # Final mutual ONLINE sanity on active sessions.
  Wait-RemoteState (Get-LogPath $B) $sb3 $uidA "online" $ConnectTimeoutSec `
      "B final remote A ONLINE" | Out-Null
  Assert-LatestLocalOnline (Get-LogPath $A) $sa3 "A final"
  Assert-LatestLocalOnline (Get-LogPath $B) $sb3 "B final"
  Assert-NoSharedTransportInSession (Get-LogPath $A) $sa3 "A final Shared check"
  Assert-NoSharedTransportInSession (Get-LogPath $B) $sb3 "B final Shared check"

  Write-Step "PASS Presence-only A/B validation"
  Write-Host ("TIMINGS offlineA_by_B={0}s offlineB_by_A={1}s" -f $dtOfflineA, $dtOfflineB)
  Write-Host ("SESSIONS sa2={0} sb2={1} sa3={2} sb3={3}" -f $sa2, $sb2, $sa3, $sb3)
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
