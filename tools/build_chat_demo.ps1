# Incremental MSVC Debug build for chat demo targets.
# Usage (from repo root):
#   powershell -File tools/build_chat_demo.ps1
# Optional: -BuildDir path -Configure

param(
  [string]$BuildDir = "build/win64-ninja-msvc-debug",
  [switch]$Configure
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$Vcvars = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $Vcvars)) {
  $Vcvars = "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
}
if (-not (Test-Path $Vcvars)) {
  Write-Error "vcvars64.bat not found"
}

if ($Configure -or -not (Test-Path "$BuildDir/CMakeCache.txt")) {
  cmd /c "`"$Vcvars`" && cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Debug"
}

$targets = @(
  "apptraverse_chat",
  "apptraverse_chat_demo_model_test",
  "apptraverse_chat_session_integration_test",
  "apptraverse_chat_session_fault_test",
  "apptraverse_chat_session_command_limits_test",
  "apptraverse_chat_windows_smoke_test",
  "apptraverse_shared_sync_protocol_test"
)

foreach ($t in $targets) {
  cmd /c "`"$Vcvars`" && cmake --build $BuildDir --target $t"
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "Built chat demo targets in $BuildDir"
