# Two-process Windows A/B latency harness for chat-component v5 diagnostics.
# Distills fresh Alice/Bob state, pairs both ways, then sends N Alice->Bob
# messages one at a time and prints min/median/max one-way deltas.

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ExePath,

  [int]$RxIntervalMs = 0,

  [int]$RetryIntervalMs = 0,

  [int]$MessageCount = 5,

  [int]$ClientReadyTimeoutSec = 180,

  [int]$SyncTimeoutSec = 240,

  [int]$MessageTimeoutSec = 90
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
  $script_dir = Split-Path -Parent $PSCommandPath
  return (Resolve-Path (Join-Path $script_dir "..\..")).Path
}

function Start-WindowsRedirected([string]$Exe, [string]$WorkingDir, [string]$Arguments, [string]$LogPath) {
  $cmd = "cd /d `"$WorkingDir`" && `"$Exe`" $Arguments > `"$LogPath`" 2>&1"
  return Start-Process -FilePath "cmd.exe" -ArgumentList @("/c", $cmd) -PassThru -WindowStyle Hidden
}

function Stop-WindowsChat {
  Get-Process win32_single_client_chat -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
}

function Wait-WindowsMarker {
  param(
    [System.Diagnostics.Process]$Process,
    [string]$LogPath,
    [string]$Pattern,
    [string]$Description,
    [int]$TimeoutSec
  )
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if (Test-Path $LogPath) {
      $text = Get-Content -Raw -Path $LogPath -ErrorAction SilentlyContinue
      if ($null -ne $text) {
        $match = ($text -split "`n" | Where-Object { $_ -match $Pattern } | Select-Object -First 1)
        if ($match) {
          Write-Host "  OK  $Description"
          return $match.Trim()
        }
      }
    }
    if ($Process.HasExited) {
      $text = if (Test-Path $LogPath) { Get-Content -Raw $LogPath } else { "" }
      throw "Windows process exited early (code=$($Process.ExitCode)) while waiting for $Description`n$text"
    }
    Start-Sleep -Milliseconds 400
  }
  $text = if (Test-Path $LogPath) { Get-Content -Raw $LogPath } else { "" }
  throw "Timed out waiting for Windows $Description`n$text"
}

function Get-UidFromMarker([string]$Line) {
  if ($Line -match "AETHER_UID=([0-9a-fA-F-]+)") {
    return $Matches[1]
  }
  if ($Line -match "AETHER_CLIENT_READY platform=windows uid=([0-9a-fA-F-]+)") {
    return $Matches[1]
  }
  throw "Unable to parse Aether UID from: $Line"
}

function Get-Field([string]$Line, [string]$Name) {
  if ($Line -match "$Name=([0-9]+)") {
    return [int64]$Matches[1]
  }
  return $null
}

function Get-Median([int64[]]$Values) {
  if ($null -eq $Values -or $Values.Count -eq 0) {
    return $null
  }
  $sorted = $Values | Sort-Object
  $mid = [int][Math]::Floor(($sorted.Count - 1) / 2)
  if ($sorted.Count % 2 -eq 1) {
    return $sorted[$mid]
  }
  return [int64][Math]::Round(($sorted[$mid] + $sorted[$mid + 1]) / 2.0)
}

function Write-Inbox([string]$Path, [string]$Line) {
  $dir = Split-Path -Parent $Path
  if (-not (Test-Path $dir)) {
    New-Item -ItemType Directory -Path $dir | Out-Null
  }
  Set-Content -LiteralPath $Path -Value $Line -Encoding ascii
}

function OptionalFlag([string]$Name, [int]$Value) {
  if ($Value -gt 0) {
    return "$Name $Value"
  }
  return ""
}

$repo_root = Resolve-RepoRoot
$exe = (Resolve-Path $ExePath).Path
if (-not (Test-Path $exe)) {
  throw "ExePath not found: $ExePath"
}

$out_dir = Join-Path $repo_root "build\latency-ab"
$alice_state = Join-Path $out_dir "alice"
$bob_state = Join-Path $out_dir "bob"
$alice_peer_inbox = Join-Path $out_dir "alice.peer-inbox"
$bob_peer_inbox = Join-Path $out_dir "bob.peer-inbox"
$alice_commit_inbox = Join-Path $out_dir "alice.commit-inbox"
$bob_commit_inbox = Join-Path $out_dir "bob.commit-inbox"
$alice_uid_log = Join-Path $out_dir "alice_uid.log"
$bob_uid_log = Join-Path $out_dir "bob_uid.log"
$alice_log = Join-Path $out_dir "alice.log"
$bob_log = Join-Path $out_dir "bob.log"

$alice_name = "apptraverse-windows-ab-alice"
$bob_name = "apptraverse-windows-ab-bob"

Write-Host "Repo                 : $repo_root"
Write-Host "Exe                  : $exe"
Write-Host "Out dir              : $out_dir"
Write-Host "RxIntervalMs         : $RxIntervalMs"
Write-Host "RetryIntervalMs      : $RetryIntervalMs"
Write-Host "MessageCount         : $MessageCount"

Stop-WindowsChat
if (Test-Path $out_dir) {
  Remove-Item -Recurse -Force $out_dir
}
New-Item -ItemType Directory -Path $alice_state | Out-Null
New-Item -ItemType Directory -Path $bob_state | Out-Null

Write-Host ""
Write-Host "Distilling Alice and Bob"
& $exe --distill --state-dir $alice_state --aether-client-name $alice_name
if ($LASTEXITCODE -ne 0) { throw "Alice --distill failed" }
& $exe --distill --state-dir $bob_state --aether-client-name $bob_name
if ($LASTEXITCODE -ne 0) { throw "Bob --distill failed" }

Write-Host ""
Write-Host "Collecting Aether UIDs"
$alice_uid_proc = Start-WindowsRedirected $exe $repo_root `
  "--state-dir `"$alice_state`" --aether-client-name $alice_name --print-aether-uid" `
  $alice_uid_log
$bob_uid_proc = Start-WindowsRedirected $exe $repo_root `
  "--state-dir `"$bob_state`" --aether-client-name $bob_name --print-aether-uid" `
  $bob_uid_log
try {
  $alice_uid_line = Wait-WindowsMarker $alice_uid_proc $alice_uid_log "AETHER_UID=" "Alice AETHER_UID" $ClientReadyTimeoutSec
  $bob_uid_line = Wait-WindowsMarker $bob_uid_proc $bob_uid_log "AETHER_UID=" "Bob AETHER_UID" $ClientReadyTimeoutSec
  $alice_uid = Get-UidFromMarker $alice_uid_line
  $bob_uid = Get-UidFromMarker $bob_uid_line
} finally {
  if (-not $alice_uid_proc.HasExited) { Stop-Process -Id $alice_uid_proc.Id -Force -ErrorAction SilentlyContinue }
  if (-not $bob_uid_proc.HasExited) { Stop-Process -Id $bob_uid_proc.Id -Force -ErrorAction SilentlyContinue }
  Stop-WindowsChat
}
Write-Host "  Alice UID = $alice_uid"
Write-Host "  Bob UID   = $bob_uid"

$extra_flags = @(
  (OptionalFlag "--rx-interval-ms" $RxIntervalMs),
  (OptionalFlag "--retry-interval-ms" $RetryIntervalMs)
) | Where-Object { $_ -ne "" }
$extra = ($extra_flags -join " ")

$alice_args = @(
  "--state-dir `"$alice_state`"",
  "--aether-client-name $alice_name",
  "--auto-accept-peer",
  "--peer-inbox `"$alice_peer_inbox`"",
  "--commit-inbox `"$alice_commit_inbox`"",
  $extra
) | Where-Object { $_ -ne "" }
$bob_args = @(
  "--state-dir `"$bob_state`"",
  "--aether-client-name $bob_name",
  "--auto-accept-peer",
  "--peer-inbox `"$bob_peer_inbox`"",
  "--commit-inbox `"$bob_commit_inbox`"",
  $extra
) | Where-Object { $_ -ne "" }

Write-Host ""
Write-Host "Launching Alice and Bob"
Remove-Item $alice_log, $bob_log -ErrorAction SilentlyContinue
$alice_proc = Start-WindowsRedirected $exe $repo_root ($alice_args -join " ") $alice_log
$bob_proc = Start-WindowsRedirected $exe $repo_root ($bob_args -join " ") $bob_log

try {
  Wait-WindowsMarker $alice_proc $alice_log "AETHER_CLIENT_READY platform=windows uid=" "Alice ready" $ClientReadyTimeoutSec | Out-Null
  Wait-WindowsMarker $bob_proc $bob_log "AETHER_CLIENT_READY platform=windows uid=" "Bob ready" $ClientReadyTimeoutSec | Out-Null

  Write-Host "Pairing via peer-inbox"
  Write-Inbox $alice_peer_inbox $bob_uid
  Write-Inbox $bob_peer_inbox $alice_uid
  Wait-WindowsMarker $alice_proc $alice_log "CHAT_PEER_INBOX_ADDED|CHAT_PEER_ALREADY_PRESENT|CHAT_PEER_ADDED" "Alice added Bob" $SyncTimeoutSec | Out-Null
  Wait-WindowsMarker $bob_proc $bob_log "CHAT_PEER_INBOX_ADDED|CHAT_PEER_ALREADY_PRESENT|CHAT_PEER_ADDED" "Bob added Alice" $SyncTimeoutSec | Out-Null
  Wait-WindowsMarker $alice_proc $alice_log "CHAT_SYNC_INITIAL_COMPLETE" "Alice initial sync" $SyncTimeoutSec | Out-Null
  Wait-WindowsMarker $bob_proc $bob_log "CHAT_SYNC_INITIAL_COMPLETE" "Bob initial sync" $SyncTimeoutSec | Out-Null

  Write-Host "Idle 2s"
  Start-Sleep -Seconds 2

  $commit_to_visible = @()
  $write_to_raw = @()
  $write_to_packet = @()

  for ($i = 1; $i -le $MessageCount; $i++) {
    $key = "ab_msg_$i"
    Write-Host "Send $key Alice -> Bob"
    $alice_before = if (Test-Path $alice_log) { (Get-Content -Raw $alice_log) } else { "" }
    $bob_before = if (Test-Path $bob_log) { (Get-Content -Raw $bob_log) } else { "" }
    Write-Inbox $alice_commit_inbox $key

    $commit_line = Wait-WindowsMarker $alice_proc $alice_log "CHAT_MESSAGE_COMMITTED platform=windows .*text_key=$key" "Alice committed $key" $MessageTimeoutSec
    $visible_line = Wait-WindowsMarker $bob_proc $bob_log "CHAT_MESSAGE_VISIBLE platform=windows text_key=$key" "Bob visible $key" $MessageTimeoutSec

    $alice_after = Get-Content -Raw $alice_log
    $bob_after = Get-Content -Raw $bob_log
    $alice_delta = if ($alice_after.Length -gt $alice_before.Length) { $alice_after.Substring($alice_before.Length) } else { $alice_after }
    $bob_delta = if ($bob_after.Length -gt $bob_before.Length) { $bob_after.Substring($bob_before.Length) } else { $bob_after }

    $commit_us = Get-Field $commit_line "t_us"
    $visible_us = Get-Field $visible_line "t_us"
    $write_line = ($alice_delta -split "`n" | Where-Object { $_ -match "P2P_SYNC_WRITE_ATTEMPT" } | Select-Object -First 1)
    $raw_line = ($bob_delta -split "`n" | Where-Object { $_ -match "P2P_RAW_RECEIVED" } | Select-Object -First 1)
    $packet_line = ($bob_delta -split "`n" | Where-Object { $_ -match "SYNC_PACKET_RECEIVED kind=event" } | Select-Object -First 1)
    if (-not $packet_line) {
      $packet_line = ($bob_delta -split "`n" | Where-Object { $_ -match "SYNC_PACKET_RECEIVED" } | Select-Object -First 1)
    }

    $write_us = if ($write_line) { Get-Field $write_line "t_us" } else { $null }
    $raw_us = if ($raw_line) { Get-Field $raw_line "t_us" } else { $null }
    $packet_us = if ($packet_line) { Get-Field $packet_line "t_us" } else { $null }

    if ($null -ne $commit_us -and $null -ne $visible_us) {
      $commit_to_visible += ($visible_us - $commit_us)
    }
    if ($null -ne $write_us -and $null -ne $raw_us) {
      $write_to_raw += ($raw_us - $write_us)
    }
    if ($null -ne $write_us -and $null -ne $packet_us) {
      $write_to_packet += ($packet_us - $write_us)
    }

    Write-Host ("    commit_us={0} write_us={1} raw_us={2} packet_us={3} visible_us={4}" -f `
      $commit_us, $write_us, $raw_us, $packet_us, $visible_us)
  }

  function Write-StatRow([string]$Name, [int64[]]$Values) {
    if ($null -eq $Values -or $Values.Count -eq 0) {
      Write-Host ("  {0,-28} n=0" -f $Name)
      return
    }
    $min = ($Values | Measure-Object -Minimum).Minimum
    $max = ($Values | Measure-Object -Maximum).Maximum
    $med = Get-Median $Values
    Write-Host ("  {0,-28} n={1} min_us={2} median_us={3} max_us={4}" -f $Name, $Values.Count, $min, $med, $max)
  }

  Write-Host ""
  Write-Host "Latency A/B (same-machine t_us)"
  Write-StatRow "commit -> visible" $commit_to_visible
  Write-StatRow "write -> raw_receive" $write_to_raw
  Write-StatRow "write -> SYNC_PACKET_RECEIVED" $write_to_packet
  Write-Host "Logs: $alice_log"
  Write-Host "      $bob_log"
} finally {
  if ($alice_proc -and -not $alice_proc.HasExited) {
    Stop-Process -Id $alice_proc.Id -Force -ErrorAction SilentlyContinue
  }
  if ($bob_proc -and -not $bob_proc.HasExited) {
    Stop-Process -Id $bob_proc.Id -Force -ErrorAction SilentlyContinue
  }
  Stop-WindowsChat
}