#requires -Version 5.1
<#
.SYNOPSIS
  Real two-process Win32 chat Presence A/B validation.

.DESCRIPTION
  Launches win32_chat_ui_runtime_demo as --host and --client with independent
  state dirs, connects Client->Host via UID, asserts LOCAL/REMOTE Presence
  transitions including stop/restart of each side. Exits non-zero on failure.
#>
param(
  [string]$BuildDir = "",
  [string]$WorkRoot = "",
  [int]$OnlineTimeoutSec = 90,
  [int]$OfflineTimeoutSec = 45,
  [int]$ConnectTimeoutSec = 90
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

function Get-Uid([string]$stateDir) {
  $p = Join-Path $stateDir "aether\last_uid.txt"
  if (-not (Test-Path $p)) { return $null }
  return ((Get-Content $p -Raw).Trim())
}

function Select-LogMatches([string]$logPath, [string]$pattern) {
  if (-not (Test-Path $logPath)) { return @() }
  return @(Select-String -Path $logPath -Pattern $pattern -AllMatches | ForEach-Object { $_.Line })
}

function Wait-Log([string]$logPath, [string]$pattern, [int]$timeoutSec, [string]$label) {
  $deadline = (Get-Date).AddSeconds($timeoutSec)
  while ((Get-Date) -lt $deadline) {
    $hits = Select-LogMatches $logPath $pattern
    if ($hits.Count -gt 0) {
      Write-Step ("OK {0}: {1}" -f $label, $hits[-1])
      return $hits[-1]
    }
    Start-Sleep -Milliseconds 250
  }
  throw ("TIMEOUT waiting for '{0}' in {1} ({2}s)" -f $pattern, $logPath, $timeoutSec)
}

function Assert-NoMatch([string]$logPath, [string]$badPattern, [string]$label) {
  $hits = Select-LogMatches $logPath $badPattern
  if ($hits.Count -gt 0) {
    throw ("ASSERT {0}: unexpected match '{1}': {2}" -f $label, $badPattern, $hits[-1])
  }
}

function Start-Chat([string]$role, [string]$stateDir, [string]$connectHostUid = "") {
  $args = @("--$role", "--state-dir", $stateDir)
  if ($connectHostUid) {
    $args += @("--connect-host-uid", $connectHostUid)
  }
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

function Wait-LocalOnline([string]$logPath, [int]$timeoutSec, [string]$label) {
  # Initial UNKNOWN must appear before ONLINE (or at least ONLINE must not be the only early state).
  $unknown = Select-LogMatches $logPath "LOCAL_PRESENCE state=unknown"
  if ($unknown.Count -eq 0) {
    Wait-Log $logPath "LOCAL_PRESENCE state=unknown" $timeoutSec ("$label local UNKNOWN")
  } else {
    Write-Step ("OK {0} local UNKNOWN already logged" -f $label)
  }
  Wait-Log $logPath "LOCAL_PRESENCE state=online" $timeoutSec ("$label local ONLINE")
}

function Wait-RemoteState([string]$logPath, [string]$peerUid, [string]$state, [int]$timeoutSec, [string]$label) {
  $pattern = "REMOTE_PRESENCE peer=$peerUid state=$state"
  Wait-Log $logPath $pattern $timeoutSec $label
}

function Wait-UiContact([string]$logPath, [string]$uid, [string]$state, [int]$timeoutSec, [string]$label) {
  $pattern = "UI_PRESENCE contacts=.*${uid}:${state}"
  Wait-Log $logPath $pattern $timeoutSec $label
}

function Send-ConnectKeys([System.Diagnostics.Process]$proc, [string]$hostUid) {
  # Focus client window and type Host UID + Alt+C is not reliable; use clipboard paste via UI is complex.
  # Instead write connect request file consumed... chat has no file API.
  # Use Win32 SendMessage is heavy. Prefer automating via the connection bar by posting WM.
  # Practical approach for this demo: write host UID into a helper that uses UI Automation is out of scope.
  # The chat Client expects typing in Host Aether ID edit + Connect.
  Add-Type -AssemblyName System.Windows.Forms
  Add-Type -AssemblyName Microsoft.VisualBasic
  Start-Sleep -Milliseconds 500
  [Microsoft.VisualBasic.Interaction]::AppActivate($proc.Id) | Out-Null
  Start-Sleep -Milliseconds 400
  # Tab into Host Aether ID field (Client layout: label, edit, Connect).
  [System.Windows.Forms.SendKeys]::SendWait("^a")
  Start-Sleep -Milliseconds 100
  [System.Windows.Forms.SendKeys]::SendWait($hostUid)
  Start-Sleep -Milliseconds 200
  [System.Windows.Forms.SendKeys]::SendWait("%{C}") # Alt+C if accelerator; fallback Enter after Tab to Connect
  Start-Sleep -Milliseconds 200
  [System.Windows.Forms.SendKeys]::SendWait("{TAB}{ENTER}")
}

# --- main ---
Stop-ChatProcesses
Reset-Dir $WorkRoot
$A = Join-Path $WorkRoot "A"
$B = Join-Path $WorkRoot "B"
Reset-Dir $A
Reset-Dir $B

Write-Step "Distill A (Host) / B (Client)"
& $Exe --distill --state-dir $A --host-name Host | Out-Null
& $Exe --distill --state-dir $B --host-name Client | Out-Null

$procA = Start-Chat "host" $A
try {
  Wait-LocalOnline (Get-LogPath $A) $OnlineTimeoutSec "A"
  $uidA = $null
  $deadline = (Get-Date).AddSeconds($OnlineTimeoutSec)
  while ((Get-Date) -lt $deadline -and -not $uidA) {
    $uidA = Get-Uid $A
    Start-Sleep -Milliseconds 200
  }
  if (-not $uidA) {
    throw "Failed to read Host Aether UID from state dir"
  }
  Write-Step "UID A=$uidA"

  $procB = Start-Chat "client" $B $uidA
  Wait-LocalOnline (Get-LogPath $B) $OnlineTimeoutSec "B"
  $uidB = $null
  $deadline = (Get-Date).AddSeconds($OnlineTimeoutSec)
  while ((Get-Date) -lt $deadline -and -not $uidB) {
    $uidB = Get-Uid $B
    Start-Sleep -Milliseconds 200
  }
  if (-not $uidB) {
    throw "Failed to read Client Aether UID from state dir"
  }
  Write-Step "UID B=$uidB"

  # Ask Host to QueryPeerPresence(B) without waiting on inbound P2P Join.
  Set-Content -Path (Join-Path $A "monitor_peer_uid.txt") -Value $uidB -NoNewline
  Write-Step "Wrote A/monitor_peer_uid.txt for B"

  # Assert local UI presence ONLINE for self (uid:online appears).
  Wait-UiContact (Get-LogPath $A) $uidA "online" $OnlineTimeoutSec "A UI local ONLINE"
  Wait-UiContact (Get-LogPath $B) $uidB "online" $OnlineTimeoutSec "B UI local ONLINE"

  Write-Step "Client started with --connect-host-uid; waiting for remote Presence"

  # After connect + contact ensure, each side should monitor the other and show remote ONLINE.
  Wait-RemoteState (Get-LogPath $B) $uidA "online" $ConnectTimeoutSec "B sees A ONLINE"
  Wait-RemoteState (Get-LogPath $A) $uidB "online" $ConnectTimeoutSec "A sees B ONLINE"
  Wait-UiContact (Get-LogPath $B) $uidA "online" $ConnectTimeoutSec "B UI remote A ONLINE"
  Wait-UiContact (Get-LogPath $A) $uidB "online" $ConnectTimeoutSec "A UI remote B ONLINE"

  Write-Step "Stop A; B must stay local ONLINE and see A OFFLINE"
  $t0 = Get-Date
  Stop-Pid $procA "A"
  $procA = $null
  Wait-RemoteState (Get-LogPath $B) $uidA "offline" $OfflineTimeoutSec "B sees A OFFLINE"
  Wait-Log (Get-LogPath $B) "LOCAL_PRESENCE state=online" 5 "B still local ONLINE (recent)"
  # Ensure B did not flip local to offline as a side-effect of remote loss:
  $bLocal = Select-LogMatches (Get-LogPath $B) "LOCAL_PRESENCE state="
  if ($bLocal[-1] -notmatch "state=online") {
    throw ("B local presence not ONLINE after A stop: {0}" -f $bLocal[-1])
  }
  $dtOfflineA = [int]((Get-Date) - $t0).TotalSeconds
  Write-Step ("Timing: A->OFFLINE observed by B in {0}s" -f $dtOfflineA)

  Write-Step "Restart A (same state dir)"
  $procA = Start-Chat "host" $A
  Wait-LocalOnline (Get-LogPath $A) $OnlineTimeoutSec "A restarted"
  Set-Content -Path (Join-Path $A "monitor_peer_uid.txt") -Value $uidB -NoNewline
  Wait-RemoteState (Get-LogPath $B) $uidA "online" $ConnectTimeoutSec "B sees A ONLINE again"
  Wait-UiContact (Get-LogPath $B) $uidA "online" $ConnectTimeoutSec "B UI A ONLINE again"

  Write-Step "Stop B; A must stay local ONLINE and see B OFFLINE"
  $t1 = Get-Date
  Stop-Pid $procB "B"
  $procB = $null
  Wait-RemoteState (Get-LogPath $A) $uidB "offline" $OfflineTimeoutSec "A sees B OFFLINE"
  $aLocal = Select-LogMatches (Get-LogPath $A) "LOCAL_PRESENCE state="
  if ($aLocal[-1] -notmatch "state=online") {
    throw ("A local presence not ONLINE after B stop: {0}" -f $aLocal[-1])
  }
  $dtOfflineB = [int]((Get-Date) - $t1).TotalSeconds
  Write-Step ("Timing: B->OFFLINE observed by A in {0}s" -f $dtOfflineB)

  Write-Step "Restart B (same state dir)"
  $procB = Start-Chat "client" $B $uidA
  Wait-LocalOnline (Get-LogPath $B) $OnlineTimeoutSec "B restarted"
  Set-Content -Path (Join-Path $A "monitor_peer_uid.txt") -Value $uidB -NoNewline
  Wait-RemoteState (Get-LogPath $A) $uidB "online" $ConnectTimeoutSec "A sees B ONLINE again"
  Wait-UiContact (Get-LogPath $A) $uidB "online" $ConnectTimeoutSec "A UI B ONLINE again"

  Write-Step "PASS chat Presence A/B validation"
  Write-Host ("TIMINGS offlineA_by_B={0}s offlineB_by_A={1}s" -f $dtOfflineA, $dtOfflineB)
  exit 0
}
catch {
  Write-Host ("FAIL: {0}" -f $_.Exception.Message) -ForegroundColor Red
  if (Test-Path (Get-LogPath $A)) {
    Write-Host "---- A log (tail) ----"
    Get-Content (Get-LogPath $A) -Tail 40
  }
  if (Test-Path (Get-LogPath $B)) {
    Write-Host "---- B log (tail) ----"
    Get-Content (Get-LogPath $B) -Tail 40
  }
  exit 1
}
finally {
  Stop-Pid $procA "A"
  Stop-Pid $procB "B"
}
