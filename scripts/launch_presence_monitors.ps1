param(
  [string]$Root = "",
  [switch]$FreshState
)

$ErrorActionPreference = "Stop"

if (-not $Root) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$BuildDir = Join-Path $Root "build\win64-ninja-msvc-debug"
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

if (-not (Test-Path $BuiltExe)) {
  throw "Build first: $BuiltExe"
}

New-Item -ItemType Directory -Force -Path (Split-Path $BinA), (Split-Path $BinB), $IdsDir, $LogsDir | Out-Null
Copy-Item -Force $BuiltExe $BinA
Copy-Item -Force $BuiltExe $BinB

if ($FreshState) {
  if (Test-Path $StateA) { Remove-Item -Recurse -Force $StateA }
  if (Test-Path $StateB) { Remove-Item -Recurse -Force $StateB }
}

New-Item -ItemType Directory -Force -Path $StateA, $StateB | Out-Null

if (-not (Test-Path $UidA) -or $FreshState) {
  Write-Host "Registering A..."
  $procA = Start-Process -FilePath $BinA -ArgumentList @(
    "--register-only", "--state-dir", $StateA, "--id-out", $UidA,
    "--label", "A", "--client-name", "presence-monitor-A"
  ) -Wait -PassThru -NoNewWindow
  if ($procA.ExitCode -ne 0 -or -not (Test-Path $UidA)) { throw "Register A failed" }
}

if (-not (Test-Path $UidB) -or $FreshState) {
  Write-Host "Registering B..."
  $procB = Start-Process -FilePath $BinB -ArgumentList @(
    "--register-only", "--state-dir", $StateB, "--id-out", $UidB,
    "--label", "B", "--client-name", "presence-monitor-B"
  ) -Wait -PassThru -NoNewWindow
  if ($procB.ExitCode -ne 0 -or -not (Test-Path $UidB)) { throw "Register B failed" }
}

$AUid = (Get-Content $UidA -Raw).Trim()
$BUid = (Get-Content $UidB -Raw).Trim()
Write-Host "A UID: $AUid"
Write-Host "B UID: $BUid"

Get-Process aether_presence_monitor_A, aether_presence_monitor_B -ErrorAction SilentlyContinue |
  Stop-Process -Force -ErrorAction SilentlyContinue

$ALog = Join-Path $LogsDir "A.jsonl"
$BLog = Join-Path $LogsDir "B.jsonl"

Write-Host "Launching monitor A (peer=B)..."
Start-Process -FilePath $BinA -ArgumentList @(
  "--monitor", "--state-dir", $StateA, "--peer-id", $BUid, "--label", "A",
  "--log", $ALog, "--client-name", "presence-monitor-A",
  "--window-x", "40", "--window-y", "80"
) | Out-Null

Write-Host "Launching monitor B (peer=A)..."
Start-Process -FilePath $BinB -ArgumentList @(
  "--monitor", "--state-dir", $StateB, "--peer-id", $AUid, "--label", "B",
  "--log", $BLog, "--client-name", "presence-monitor-B",
  "--window-x", "560", "--window-y", "80"
) | Out-Null

Write-Host "Done. Two GUI windows should be visible."
