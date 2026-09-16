# Incremental MSVC build for chat demo targets.
# Isolates vcvars + CMake + VS Ninja in this process; does not edit the user PATH.
#
# Usage (from repo root):
#   powershell -File tools/build_chat_demo.ps1
#   powershell -File tools/build_chat_demo.ps1 -BuildDir build/chat-r2-msvc-debug -Configure
#   powershell -File tools/build_chat_demo.ps1 -AetherDemos:$false

param(
  [string]$BuildDir = "build/win64-ninja-msvc-debug",
  [string]$Configuration = "Debug",
  [switch]$Configure,
  [bool]$AetherDemos = $true,
  [string[]]$Targets = @(),
  [string]$LibsodiumSource = "",
  [string]$LibbcryptSource = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

function Invoke-VcCmd([string]$CommandLine) {
  $batch = Join-Path $env:TEMP ("apptraverse_build_{0}.cmd" -f [guid]::NewGuid().ToString("N"))
  @(
    "@echo off"
    "setlocal EnableExtensions"
    "call `"$script:Vcvars`""
    "if errorlevel 1 exit /b 1"
    "set `"PATH=$script:CMakeDir;$script:NinjaDir;%PATH%`""
    "set `"PATH=%PATH:C:\msys64\ucrt64\bin;=%`""
    "set `"PATH=%PATH:C:\msys64\usr\bin;=%`""
    $CommandLine
    "exit /b %ERRORLEVEL%"
  ) | Set-Content -LiteralPath $batch -Encoding ASCII
  cmd.exe /c "`"$batch`""
  $code = $LASTEXITCODE
  Remove-Item -LiteralPath $batch -ErrorAction SilentlyContinue
  if ($code -ne 0) {
    Write-Error "Native command failed with exit $code : $CommandLine"
    exit $code
  }
}

$VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $VsWhere)) {
  Write-Error "vswhere.exe not found"
  exit 1
}
$VsInstall = (& $VsWhere -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath | Select-Object -First 1).Trim()
$script:Vcvars = Join-Path $VsInstall "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path -LiteralPath $script:Vcvars)) {
  Write-Error "vcvars64.bat not found at $script:Vcvars"
  exit 1
}

$CMake = Join-Path ${env:ProgramFiles} "CMake\bin\cmake.exe"
$script:CMakeDir = Join-Path ${env:ProgramFiles} "CMake\bin"
$script:NinjaDir = Join-Path $VsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$Ninja = Join-Path $script:NinjaDir "ninja.exe"
if (-not (Test-Path -LiteralPath $CMake)) {
  Write-Error "CMake not found at $CMake"
  exit 1
}
if (-not (Test-Path -LiteralPath $Ninja)) {
  Write-Error "VS Ninja not found at $Ninja"
  exit 1
}

$GitSha = (git rev-parse HEAD).Trim()
$DirtyFlag = if (git status --porcelain) { "dirty" } else { "clean" }
Write-Host "APPTRAVERSE_SOURCE_DIR=$Root"
Write-Host "APPTRAVERSE_SOURCE_SHA=$GitSha"
Write-Host "APPTRAVERSE_SOURCE_TREE=$DirtyFlag"
Write-Host "APPTRAVERSE_VS_INSTALL=$VsInstall"
Write-Host "APPTRAVERSE_CMAKE=$CMake"
Write-Host "APPTRAVERSE_NINJA=$Ninja"
Write-Host "APPTRAVERSE_BUILD_DIR=$BuildDir"
Write-Host "APPTRAVERSE_CONFIGURATION=$Configuration"

$AbsBuild = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $Root $BuildDir }
New-Item -ItemType Directory -Force -Path $AbsBuild | Out-Null
$NeedConfigure = $Configure -or -not (Test-Path -LiteralPath (Join-Path $AbsBuild "CMakeCache.txt"))
$AetherValue = if ($AetherDemos) { "ON" } else { "OFF" }

if ($NeedConfigure) {
  Write-Host "Configuring $AbsBuild"
  $qRoot = $Root
  $qBuild = $AbsBuild
  $qNinja = $Ninja
  # Prefer already-patched Windows SOURCE trees when present (CPM PATCHES may not
  # re-apply on cached downloads). Do not invent paths; only use existing trees.
  if (-not $LibsodiumSource) {
    $candidates = @(
      (Join-Path $Root "build\win64-ninja-msvc-debug\_deps\libsodium-src"),
      "C:\Users\nickc\Projects\apptraverse-surfaces-integration\build\win64-ninja-msvc-debug\_deps\libsodium-src"
    )
    foreach ($c in $candidates) {
      if ((Test-Path -LiteralPath (Join-Path $c "CMakeLists.txt")) -and
          (Test-Path -LiteralPath (Join-Path $c "src\libsodium\include\sodium.h"))) {
        $LibsodiumSource = $c
        break
      }
    }
  }
  if (-not $LibbcryptSource) {
    $candidates = @(
      (Join-Path $Root "build\win64-ninja-msvc-debug\_deps\libbcrypt-fixed"),
      (Join-Path $Root "build\win64-ninja-msvc-debug\_deps\libbcrypt-src")
    )
    foreach ($c in $candidates) {
      if (Test-Path -LiteralPath (Join-Path $c "CMakeLists.txt")) {
        $LibbcryptSource = $c
        break
      }
    }
  }
  $extra = "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
  if ($LibsodiumSource) {
    $LibsodiumSource = ($LibsodiumSource -replace '\\', '/')
    Write-Host "APPTRAVERSE_LIBSODIUM_SOURCE=$LibsodiumSource"
    $extra += " `"-DCPM_libsodium_SOURCE=$LibsodiumSource`""
  }
  if ($LibbcryptSource) {
    $LibbcryptSource = ($LibbcryptSource -replace '\\', '/')
    Write-Host "APPTRAVERSE_LIBBCRYPT_SOURCE=$LibbcryptSource"
    $extra += " `"-DCPM_libbcrypt_SOURCE=$LibbcryptSource`""
  }
  Invoke-VcCmd " `"$CMake`" -S `"$qRoot`" -B `"$qBuild`" -G Ninja -DCMAKE_BUILD_TYPE=$Configuration -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `"-DCMAKE_MAKE_PROGRAM=$qNinja`" $extra -DAPPTRAVERSE_BUILD_AETHER_DEMOS=$AetherValue"
}

if ($Targets.Count -eq 0) {
  if ($AetherDemos) {
    $Targets = @(
      "apptraverse_chat",
      "apptraverse_chat_demo_model_test",
      "apptraverse_chat_session_integration_test",
      "apptraverse_chat_session_fault_test",
      "apptraverse_chat_session_command_limits_test",
      "apptraverse_chat_windows_smoke_test",
      "apptraverse_shared_sync_protocol_test"
    )
  } else {
    $Targets = @(
      "apptraverse_chat_demo_model_test",
      "apptraverse_chat_demo_launch_options_test"
    )
  }
}

foreach ($t in $Targets) {
  Write-Host "Building $t"
  Invoke-VcCmd " `"$CMake`" --build `"$AbsBuild`" --config $Configuration --target $t"
}

Write-Host "Built chat demo targets in $AbsBuild exit=0"
exit 0
