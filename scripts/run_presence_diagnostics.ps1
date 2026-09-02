param(
  [ValidateSet("test1", "test2", "gui")]
  [string]$Phase = "gui",
  [string]$Root = "",
  [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

if (-not $Root) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

if (-not $BuildDir) {
  $candidates = @(
    (Join-Path $Root "build\win64-ninja-msvc-release"),
    (Join-Path $Root "build-win64-resize-release"),
    (Join-Path $Root "build\win64-ninja-msvc-debug")
  )
  foreach ($c in $candidates) {
    if (Test-Path (Join-Path $c "examples\aether_presence_monitor\aether_presence_monitor.exe")) {
      $BuildDir = $c
      break
    }
  }
  if (-not $BuildDir) {
    throw "No built aether_presence_monitor.exe found. Build first."
  }
}

$BuiltExe = Join-Path $BuildDir "examples\aether_presence_monitor\aether_presence_monitor.exe"
$Artifacts = Join-Path $Root "artifacts\presence-monitor"
$BinA = Join-Path $Artifacts "bin\A\aether_presence_monitor_A.exe"
$BinB = Join-Path $Artifacts "bin\B\aether_presence_monitor_B.exe"
$StateA = Join-Path $Artifacts "state\A"
$StateB = Join-Path $Artifacts "state\B"
$IdsDir = Join-Path $Artifacts "ids"
$LogsDir = Join-Path $Artifacts "logs"
$UidA = Join-Path $IdsDir "A.uid"
$UidB = Join-Path $IdsDir "B.uid"

$ExpectedA = "a2c9729e-7870-4039-bd59-5d722279b685"
$ExpectedB = "42e7d356-23b5-43ae-b32c-b3bc120fb5ac"

New-Item -ItemType Directory -Force -Path (Split-Path $BinA), (Split-Path $BinB), $IdsDir, $LogsDir, $StateA, $StateB | Out-Null

Get-Process aether_presence_monitor_A, aether_presence_monitor_B -ErrorAction SilentlyContinue |
  Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

Copy-Item -Force $BuiltExe $BinA
Copy-Item -Force $BuiltExe $BinB

if (-not (Test-Path $UidA)) {
  Set-Content -Path $UidA -Value $ExpectedA -NoNewline
}
if (-not (Test-Path $UidB)) {
  Set-Content -Path $UidB -Value $ExpectedB -NoNewline
}

$AUid = (Get-Content $UidA -Raw).Trim()
$BUid = (Get-Content $UidB -Raw).Trim()
Write-Host "A UID: $AUid"
Write-Host "B UID: $BUid"

function Get-LocalSummary([string]$LogPath) {
  if (-not (Test-Path $LogPath)) { return $null }
  $lines = Get-Content $LogPath -Tail 200 -ErrorAction SilentlyContinue
  $last = $lines | Where-Object { $_ -match '"event":"LOCAL_STATE"' } | Select-Object -Last 1
  if (-not $last) { return $null }
  return $last | ConvertFrom-Json
}

function Get-RemoteSummary([string]$LogPath) {
  if (-not (Test-Path $LogPath)) { return $null }
  $lines = Get-Content $LogPath -Tail 200 -ErrorAction SilentlyContinue
  $last = $lines | Where-Object { $_ -match '"event":"REMOTE_QUERY_RESULT"' } | Select-Object -Last 1
  if (-not $last) { return $null }
  return $last | ConvertFrom-Json
}

function Count-LocalTransitions([string]$LogPath, [datetime]$Since) {
  if (-not (Test-Path $LogPath)) { return 0 }
  $count = 0
  foreach ($line in Get-Content $LogPath) {
    if ($line -notmatch '"event":"LOCAL_STATE"') { continue }
    try {
      $obj = $line | ConvertFrom-Json
      if ($obj.timestamp) {
        $ts = [datetime]$obj.timestamp
        if ($ts -lt $Since) { continue }
      }
      $count++
    } catch {}
  }
  return [Math]::Max(0, $count - 1)
}

$commonArgs = @("--ping-ms", "1000", "--window-ms", "1000", "--peer-ping-ms", "1000")

if ($Phase -eq "test1") {
  $TestStart = Get-Date
  $ALog = Join-Path $LogsDir "test1_A.jsonl"
  $BLog = Join-Path $LogsDir "test1_B.jsonl"
  Remove-Item -Force $ALog, $BLog -ErrorAction SilentlyContinue

  Write-Host "TEST1: local ping only, 120s, no remote queries"
  $argsA = @(
    "--monitor", "--state-dir", $StateA, "--peer-id", $BUid, "--label", "A",
    "--log", $ALog, "--client-name", "presence-monitor-A",
    "--window-x", "40", "--window-y", "80", "--no-remote-queries",
    "--auto-exit-sec", "120"
  ) + $commonArgs
  $argsB = @(
    "--monitor", "--state-dir", $StateB, "--peer-id", $AUid, "--label", "B",
    "--log", $BLog, "--client-name", "presence-monitor-B",
    "--window-x", "560", "--window-y", "80", "--no-remote-queries",
    "--auto-exit-sec", "120"
  ) + $commonArgs
  $procA = Start-Process -FilePath $BinA -ArgumentList $argsA -PassThru -WindowStyle Normal
  $procB = Start-Process -FilePath $BinB -ArgumentList $argsB -PassThru -WindowStyle Normal
  Wait-Process -Id $procA.Id, $procB.Id

  Write-Host "TEST1 complete. A exit=$($procA.ExitCode) B exit=$($procB.ExitCode)"
  Write-Host "A local transitions (approx): $(Count-LocalTransitions $ALog $TestStart)"
  Write-Host "B local transitions (approx): $(Count-LocalTransitions $BLog $TestStart)"
  exit 0
}

if ($Phase -eq "test2") {
  $TestStart = Get-Date
  $ALog = Join-Path $LogsDir "test2_A.jsonl"
  $BLog = Join-Path $LogsDir "test2_B.jsonl"
  Remove-Item -Force $ALog, $BLog -ErrorAction SilentlyContinue

  Write-Host "TEST2: remote load, query period 333ms after completion, 180s"
  $argsA = @(
    "--monitor", "--state-dir", $StateA, "--peer-id", $BUid, "--label", "A",
    "--log", $ALog, "--client-name", "presence-monitor-A",
    "--window-x", "40", "--window-y", "80",
    "--query-period-ms", "333", "--auto-exit-sec", "180"
  ) + $commonArgs
  $argsB = @(
    "--monitor", "--state-dir", $StateB, "--peer-id", $AUid, "--label", "B",
    "--log", $BLog, "--client-name", "presence-monitor-B",
    "--window-x", "560", "--window-y", "80",
    "--query-period-ms", "333", "--auto-exit-sec", "180"
  ) + $commonArgs
  $procA = Start-Process -FilePath $BinA -ArgumentList $argsA -PassThru -WindowStyle Normal
  $procB = Start-Process -FilePath $BinB -ArgumentList $argsB -PassThru -WindowStyle Normal
  Wait-Process -Id $procA.Id, $procB.Id

  Write-Host "TEST2 complete. A exit=$($procA.ExitCode) B exit=$($procB.ExitCode)"
  Write-Host "A local transitions (approx): $(Count-LocalTransitions $ALog $TestStart)"
  Write-Host "B local transitions (approx): $(Count-LocalTransitions $BLog $TestStart)"
  exit 0
}

# GUI phase with live summary
$ALog = Join-Path $LogsDir "gui_A.jsonl"
$BLog = Join-Path $LogsDir "gui_B.jsonl"

Write-Host "Launching GUI monitors (persistent)..."
$argsA = @(
  "--monitor", "--state-dir", $StateA, "--peer-id", $BUid, "--label", "A",
  "--log", $ALog, "--client-name", "presence-monitor-A",
  "--window-x", "40", "--window-y", "80"
) + $commonArgs
$argsB = @(
  "--monitor", "--state-dir", $StateB, "--peer-id", $AUid, "--label", "B",
  "--log", $BLog, "--client-name", "presence-monitor-B",
  "--window-x", "560", "--window-y", "80"
) + $commonArgs
Start-Process -FilePath $BinA -ArgumentList $argsA | Out-Null
Start-Process -FilePath $BinB -ArgumentList $argsB | Out-Null

Write-Host "Live summary every 5s (Ctrl+C to stop script, GUIs stay open)..."
while ($true) {
  $a = Get-LocalSummary $ALog
  $b = Get-LocalSummary $BLog
  $aRemote = Get-RemoteSummary $ALog
  $bRemote = Get-RemoteSummary $BLog

  $aLine = if ($a) {
    "A local age=$($a.last_success_age_ms)ms online=$($a.online) reason=$($a.reason)"
  } else { "A local: (waiting)" }
  $bLine = if ($b) {
    "B local age=$($b.last_success_age_ms)ms online=$($b.online) reason=$($b.reason)"
  } else { "B local: (waiting)" }
  $aRemoteLine = if ($aRemote) {
    "A->B query success=$($aRemote.success) state=$($aRemote.remote_state)"
  } else { "A->B query: (none yet)" }
  $bRemoteLine = if ($bRemote) {
    "B->A query success=$($bRemote.success) state=$($bRemote.remote_state)"
  } else { "B->A query: (none yet)" }

  Write-Host "$(Get-Date -Format 'HH:mm:ss') | $aLine | $bLine | $aRemoteLine | $bRemoteLine"
  Start-Sleep -Seconds 5
}
