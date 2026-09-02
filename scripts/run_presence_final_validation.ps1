param(
  [string]$Root = "",
  [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

if (-not $Root) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
if (-not $BuildDir) {
  $BuildDir = Join-Path $Root "build\win64-ninja-msvc-debug"
}

$Exe = Join-Path $BuildDir "examples\aether_presence_monitor\aether_presence_monitor.exe"
$Artifacts = Join-Path $Root "artifacts\presence-monitor"
$BinA = Join-Path $Artifacts "bin\A\aether_presence_monitor_A.exe"
$BinB = Join-Path $Artifacts "bin\B\aether_presence_monitor_B.exe"
$StateA = Join-Path $Artifacts "state\A"
$StateB = Join-Path $Artifacts "state\B"
$LogsDir = Join-Path $Artifacts "logs"
$UidA = (Get-Content (Join-Path $Artifacts "ids\A.uid") -Raw).Trim()
$UidB = (Get-Content (Join-Path $Artifacts "ids\B.uid") -Raw).Trim()

New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null
Copy-Item -Force $Exe $BinA
Copy-Item -Force $Exe $BinB
Get-Process aether_presence_monitor* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

function Read-Events([string]$Path) {
  if (-not (Test-Path $Path)) { return @() }
  $out = @()
  foreach ($line in Get-Content $Path) {
    if ($line.Trim().Length -eq 0) { continue }
    try { $out += ($line | ConvertFrom-Json) } catch {}
  }
  return $out
}

function Get-LastRemoteOnline([object[]]$Events) {
  $last = $Events | Where-Object { $_.event -eq "REMOTE_STATE" } | Select-Object -Last 1
  if ($last) { return $last.online -eq "true" }
  return $null
}

function Get-LastLocalOnline([object[]]$Events) {
  $last = $Events | Where-Object { $_.event -eq "LOCAL_STATE" } | Select-Object -Last 1
  if ($last) { return $last.online -eq "true" }
  return $null
}

function Get-Metrics([object[]]$Events) {
  $m = $Events | Where-Object {
    $_.event -eq "APP_STOPPED" -and $_.query_started -ne $null
  } | Select-Object -Last 1
  if (-not $m) {
    $m = $Events | Where-Object {
      $_.event -eq "AUTO_EXIT" -and $_.query_started -ne $null
    } | Select-Object -Last 1
  }
  return $m
}

function Start-Monitor([string]$Label, [string]$LogPath, [string[]]$ExtraArgs) {
  $state = if ($Label -eq "A") { $StateA } else { $StateB }
  $peer = if ($Label -eq "A") { $UidB } else { $UidA }
  $bin = if ($Label -eq "A") { $BinA } else { $BinB }
  $x = if ($Label -eq "A") { "40" } else { "560" }
  $args = @(
    "--monitor", "--state-dir", $state, "--peer-id", $peer, "--label", $Label,
    "--log", $LogPath, "--client-name", "presence-monitor-$Label",
    "--window-x", $x, "--window-y", "80",
    "--ping-ms", "1000", "--window-ms", "1000", "--query-period-ms", "333"
  ) + $ExtraArgs
  return Start-Process -FilePath $bin -ArgumentList $args -PassThru -WindowStyle Hidden
}

function Wait-Remote([object[]]$Events, [bool]$WantOnline, [int]$TimeoutSec = 60) {
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    $last = $EventsRef.Value | Where-Object { $_.event -eq "REMOTE_STATE" } | Select-Object -Last 1
    if ($last -and (($last.online -eq "true") -eq $WantOnline)) {
      return $true
    }
    $EventsRef.Value = Read-Events $EventsRef.Path
  }
  return $false
}

Write-Host "=== TEST A: stop/restart A ==="
$ALog = Join-Path $LogsDir "final_stopA_A.jsonl"
$BLog = Join-Path $LogsDir "final_stopA_B.jsonl"
Remove-Item -Force $ALog, $BLog -ErrorAction SilentlyContinue
$pB = Start-Monitor "B" $BLog @()
Start-Sleep -Seconds 3
$pA = Start-Monitor "A" $ALog @()
Start-Sleep -Seconds 25
$eA = Read-Events $ALog
$eB = Read-Events $BLog
Write-Host "A+B alive: A local=$(Get-LastLocalOnline $eA) remote=$(Get-LastRemoteOnline $eA)"
Write-Host "A+B alive: B local=$(Get-LastLocalOnline $eB) remote=$(Get-LastRemoteOnline $eB)"
Stop-Process -Id $pA.Id -Force
Start-Sleep -Seconds 15
$eB = Read-Events $BLog
Write-Host "A stopped: B local=$(Get-LastLocalOnline $eB) remote=$(Get-LastRemoteOnline $eB)"
$pA = Start-Monitor "A" $ALog @()
Start-Sleep -Seconds 20
$eB = Read-Events $BLog
Write-Host "A restarted: B remote=$(Get-LastRemoteOnline $eB)"
Stop-Process -Id $pA.Id, $pB.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

Write-Host "=== TEST B: stop/restart B ==="
$ALog2 = Join-Path $LogsDir "final_stopB_A.jsonl"
$BLog2 = Join-Path $LogsDir "final_stopB_B.jsonl"
Remove-Item -Force $ALog2, $BLog2 -ErrorAction SilentlyContinue
$pA = Start-Monitor "A" $ALog2 @()
Start-Sleep -Seconds 3
$pB = Start-Monitor "B" $BLog2 @()
Start-Sleep -Seconds 25
$eA = Read-Events $ALog2
$eB = Read-Events $BLog2
Write-Host "A+B alive: A local=$(Get-LastLocalOnline $eA) remote=$(Get-LastRemoteOnline $eA)"
Write-Host "A+B alive: B local=$(Get-LastLocalOnline $eB) remote=$(Get-LastRemoteOnline $eB)"
Stop-Process -Id $pB.Id -Force
Start-Sleep -Seconds 15
$eA = Read-Events $ALog2
Write-Host "B stopped: A local=$(Get-LastLocalOnline $eA) remote=$(Get-LastRemoteOnline $eA)"
$pB = Start-Monitor "B" $BLog2 @()
Start-Sleep -Seconds 20
$eA = Read-Events $ALog2
Write-Host "B restarted: A remote=$(Get-LastRemoteOnline $eA)"
Stop-Process -Id $pA.Id, $pB.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

Write-Host "=== 2 minute sustained test ==="
$ALog3 = Join-Path $LogsDir "final_2min_A.jsonl"
$BLog3 = Join-Path $LogsDir "final_2min_B.jsonl"
Remove-Item -Force $ALog3, $BLog3 -ErrorAction SilentlyContinue
$pA = Start-Monitor "A" $ALog3 @("--auto-exit-sec", "120")
$pB = Start-Monitor "B" $BLog3 @("--auto-exit-sec", "120")
Wait-Process -Id $pA.Id, $pB.Id
$eA = Read-Events $ALog3
$eB = Read-Events $BLog3
$mA = Get-Metrics $eA
$mB = Get-Metrics $eB

function Count-FalseOffline([object[]]$Events, [string]$Domain) {
  $falseCount = 0
  $prev = $null
  foreach ($ev in ($Events | Where-Object { $_.event -eq "LOCAL_STATE" -or $_.event -eq "REMOTE_STATE" })) {
    if ($Domain -eq "local" -and $ev.event -ne "LOCAL_STATE") { continue }
    if ($Domain -eq "remote" -and $ev.event -ne "REMOTE_STATE") { continue }
    if ($ev.online -eq "false" -and $prev -eq "true") {
      $falseCount++
    }
    $prev = $ev.online
  }
  return $falseCount
}

Write-Host "A metrics: started=$($mA.query_started) completed=$($mA.query_completed) errors=$($mA.query_errors) watchdog=$($mA.query_watchdog_stuck) max_active=$($mA.max_active_underlying_queries)"
Write-Host "B metrics: started=$($mB.query_started) completed=$($mB.query_completed) errors=$($mB.query_errors) watchdog=$($mB.query_watchdog_stuck) max_active=$($mB.max_active_underlying_queries)"
Write-Host "A false local offline transitions while peer alive: (see harness)"
Write-Host "Done."
