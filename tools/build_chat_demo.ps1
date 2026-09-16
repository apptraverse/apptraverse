# Incremental MSVC build for chat demo targets.
# Imports vcvars into this process, then runs CMake/Ninja with argument arrays.
# Does not edit the user global PATH permanently.
#
# Usage (from repo root):
#   powershell -File tools/build_chat_demo.ps1
#   powershell -File tools/build_chat_demo.ps1 -BuildDir build/chat-r2-msvc-debug -Configure
#   powershell -File tools/build_chat_demo.ps1 -NoAetherDemos
# Explicit overrides only (never auto-filled from foreign checkouts):
#   -LibsodiumSource <path> -LibbcryptSource <path>

param(
  [string]$BuildDir = "build/win64-ninja-msvc-debug",
  [string]$Configuration = "Debug",
  [switch]$Configure,
  [switch]$NoAetherDemos,
  [string[]]$Targets = @(),
  [string]$LibsodiumSource = "",
  [string]$LibbcryptSource = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

function Assert-NativeExit([int]$Code, [string]$What) {
  if ($Code -ne 0) {
    Write-Error "Native command failed with exit $Code : $What"
    exit $Code
  }
}

function Import-VcVars64([string]$VcvarsPath) {
  # Only the VS install path enters this batch (ASCII under Program Files).
  # Repository/build/target paths are never written into cmd.
  $batch = Join-Path $env:TEMP ("apptraverse_vcvars_{0}.cmd" -f [guid]::NewGuid().ToString("N"))
  @(
    "@echo off"
    "setlocal EnableExtensions"
    "call `"$VcvarsPath`" >nul"
    "if errorlevel 1 exit /b 1"
    "set"
  ) | Set-Content -LiteralPath $batch -Encoding ASCII
  try {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "cmd.exe"
    $psi.Arguments = "/c `"$batch`""
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdout = $proc.StandardOutput.ReadToEnd()
    $stderr = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()
    Assert-NativeExit $proc.ExitCode "vcvars64 bootstrap ($stderr)"
    foreach ($line in ($stdout -split "`r?`n")) {
      if ($line -match '^(.*?)=(.*)$') {
        Set-Item -LiteralPath ("Env:{0}" -f $Matches[1]) -Value $Matches[2]
      }
    }
    if (-not $env:VCINSTALLDIR) {
      Write-Error "vcvars64 did not set VCINSTALLDIR"
      exit 1
    }
  } finally {
    Remove-Item -LiteralPath $batch -ErrorAction SilentlyContinue
  }
}

function Assert-OverrideSource([string]$Label, [string]$Path) {
  if (-not $Path) { return }
  if (-not (Test-Path -LiteralPath $Path)) {
    Write-Error "$Label override path does not exist: $Path"
    exit 1
  }
  $abs = (Resolve-Path -LiteralPath $Path).Path
  Write-Host "APPTRAVERSE_${Label}_SOURCE=$abs"
  if (Test-Path -LiteralPath (Join-Path $abs ".git")) {
    Push-Location -LiteralPath $abs
    try {
      $sha = (git rev-parse HEAD).Trim()
      $dirty = if (git status --porcelain) { "dirty" } else { "clean" }
      Write-Host "APPTRAVERSE_${Label}_SHA=$sha"
      Write-Host "APPTRAVERSE_${Label}_TREE=$dirty"
      if ($dirty -ne "clean") {
        Write-Host "APPTRAVERSE_${Label}_WARNING=override has local modifications; not a clean release pin"
      }
    } finally {
      Pop-Location
    }
  } else {
    Write-Host "APPTRAVERSE_${Label}_WARNING=override is not a git checkout; patch identity unknown"
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
$Vcvars = Join-Path $VsInstall "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path -LiteralPath $Vcvars)) {
  Write-Error "vcvars64.bat not found at $Vcvars"
  exit 1
}

$CMake = Join-Path ${env:ProgramFiles} "CMake\bin\cmake.exe"
$CMakeDir = Join-Path ${env:ProgramFiles} "CMake\bin"
$NinjaDir = Join-Path $VsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$Ninja = Join-Path $NinjaDir "ninja.exe"
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
Write-Host "APPTRAVERSE_AETHER_DEMOS=$(-not $NoAetherDemos)"

$AbsBuild = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $Root $BuildDir }
New-Item -ItemType Directory -Force -Path $AbsBuild | Out-Null
$CacheFile = Join-Path $AbsBuild "CMakeCache.txt"
$AetherValue = if ($NoAetherDemos) { "OFF" } else { "ON" }

$NeedConfigure = [bool]$Configure -or -not (Test-Path -LiteralPath $CacheFile)
if ((Test-Path -LiteralPath $CacheFile) -and -not $NeedConfigure) {
  $cachedType = (Select-String -LiteralPath $CacheFile -Pattern '^CMAKE_BUILD_TYPE:STRING=(.*)$' |
    Select-Object -First 1).Matches.Groups[1].Value
  $cachedDemos = (Select-String -LiteralPath $CacheFile -Pattern '^APPTRAVERSE_BUILD_AETHER_DEMOS:BOOL=(.*)$' |
    Select-Object -First 1).Matches.Groups[1].Value
  if ($cachedType -and ($cachedType -ne $Configuration)) {
    Write-Error ("CMakeCache CMAKE_BUILD_TYPE={0} mismatches requested Configuration={1}. " +
      "Use a distinct build directory (e.g. build/chat-msvc-relwithdebinfo) or pass -Configure.") -f $cachedType, $Configuration
    exit 1
  }
  if ($cachedDemos -and ($cachedDemos -ne $AetherValue)) {
    Write-Error ("CMakeCache APPTRAVERSE_BUILD_AETHER_DEMOS={0} mismatches requested AetherDemos={1}. " +
      "Pass -Configure or use a distinct build directory.") -f $cachedDemos, $AetherValue
    exit 1
  }
}
if ($Configure) {
  $NeedConfigure = $true
}

Assert-OverrideSource "LIBSODIUM" $LibsodiumSource
Assert-OverrideSource "LIBBCRYPT" $LibbcryptSource

$savedEnv = @{}
foreach ($key in [Environment]::GetEnvironmentVariables("Process").Keys) {
  $savedEnv[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
}

try {
  Import-VcVars64 $Vcvars
  $pathParts = @($CMakeDir, $NinjaDir) + @(
    ($env:PATH -split ';' | Where-Object {
      $_ -and
      ($_ -notlike '*\msys64\ucrt64\bin*') -and
      ($_ -notlike '*\msys64\usr\bin*')
    })
  )
  $env:PATH = ($pathParts -join ';')

  if ($NeedConfigure) {
    Write-Host "Configuring $AbsBuild"
    $cfgArgs = @(
      "-S", $Root,
      "-B", $AbsBuild,
      "-G", "Ninja",
      "-DCMAKE_BUILD_TYPE=$Configuration",
      "-DCMAKE_C_COMPILER=cl",
      "-DCMAKE_CXX_COMPILER=cl",
      "-DCMAKE_MAKE_PROGRAM=$Ninja",
      "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
      "-DAPPTRAVERSE_BUILD_AETHER_DEMOS=$AetherValue"
    )
    if ($LibsodiumSource) {
      $sodiumFwd = ($LibsodiumSource -replace '\\', '/')
      $cfgArgs += "-DCPM_libsodium_SOURCE=$sodiumFwd"
    }
    if ($LibbcryptSource) {
      $bcryptFwd = ($LibbcryptSource -replace '\\', '/')
      $cfgArgs += "-DCPM_libbcrypt_SOURCE=$bcryptFwd"
    }
    & $CMake @cfgArgs
    Assert-NativeExit $LASTEXITCODE "cmake configure"
  }

  if ($Targets.Count -eq 0) {
    if (-not $NoAetherDemos) {
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
        "chat_demo_model",
        "apptraverse_chat_demo_model_test",
        "apptraverse_chat_demo_launch_options_test"
      )
    }
  } else {
    # powershell -File flattens "a,b" to one string; expand commas.
    $expanded = @()
    foreach ($t in $Targets) {
      $expanded += @($t -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    }
    $Targets = $expanded
  }

  # Refresh generated identity before compiling the identity unit.
  $genDir = Join-Path $AbsBuild "generated"
  New-Item -ItemType Directory -Force -Path $genDir | Out-Null
  $genH = Join-Path $genDir "chat_build_info_generated.h"
  & $CMake `
    "-DREPO_ROOT=$Root" `
    "-DOUTPUT=$genH" `
    "-DCONFIGURATION=$Configuration" `
    "-DCXX_COMPILER_ID=MSVC" `
    -P (Join-Path $Root "cmake\generate_chat_build_info.cmake")
  Assert-NativeExit $LASTEXITCODE "generate chat build info"

  function Resolve-TargetExe([string]$Target) {
    switch ($Target) {
      "apptraverse_chat" {
        return (Join-Path $AbsBuild "examples\chat_demo\windows\apptraverse_chat.exe")
      }
      "apptraverse_chat_session_live_probe" {
        return (Join-Path $AbsBuild "tests\apptraverse_chat_session_live_probe.exe")
      }
      default {
        $cand = Join-Path $AbsBuild "tests\$Target.exe"
        if (Test-Path -LiteralPath $cand) { return $cand }
        return $null
      }
    }
  }

  function Write-BuildReceipt([string]$Target, [string]$ExePath) {
    if (-not $ExePath -or -not (Test-Path -LiteralPath $ExePath)) {
      return
    }
    $hash = (Get-FileHash -LiteralPath $ExePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $receiptDir = Join-Path $AbsBuild "receipts"
    New-Item -ItemType Directory -Force -Path $receiptDir | Out-Null
    $receiptPath = Join-Path $receiptDir ("{0}.json" -f $Target)
    $embedded = @{}
    if (Test-Path -LiteralPath $genH) {
      Get-Content -LiteralPath $genH | ForEach-Object {
        if ($_ -match '#define APPTRAVERSE_CHAT_SOURCE_SHA "([^"]+)"') {
          $embedded["binary_source_sha"] = $Matches[1]
        } elseif ($_ -match '#define APPTRAVERSE_CHAT_COMPILE_FINGERPRINT "([^"]+)"') {
          $embedded["compile_fingerprint"] = $Matches[1]
        } elseif ($_ -match '#define APPTRAVERSE_CHAT_SOURCE_DIRTY_FLAG "([^"]+)"') {
          $embedded["source_dirty"] = $Matches[1]
        } elseif ($_ -match '#define APPTRAVERSE_CHAT_BUILD_CONFIGURATION "([^"]+)"') {
          $embedded["configuration"] = $Matches[1]
        }
      }
    }
    $obj = [ordered]@{
      target = $Target
      artifact = $ExePath
      sha256 = $hash
      exit_code = 0
      embedded = $embedded
      packaging_repo_sha = $GitSha
      built_at_utc = [DateTime]::UtcNow.ToString("o")
    }
    $json = ($obj | ConvertTo-Json -Depth 5)
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($receiptPath, $json, $utf8NoBom)
    Write-Host "APPTRAVERSE_BUILD_RECEIPT=$receiptPath sha256=$hash"
  }

  foreach ($t in $Targets) {
    Write-Host "Building $t"
    & $CMake --build $AbsBuild --config $Configuration --target $t
    Assert-NativeExit $LASTEXITCODE "cmake --build --target $t"
    Write-BuildReceipt $t (Resolve-TargetExe $t)
  }
} finally {
  foreach ($key in @([Environment]::GetEnvironmentVariables("Process").Keys)) {
    if (-not $savedEnv.ContainsKey($key)) {
      Remove-Item -LiteralPath ("Env:{0}" -f $key) -ErrorAction SilentlyContinue
    }
  }
  foreach ($key in $savedEnv.Keys) {
    [Environment]::SetEnvironmentVariable($key, $savedEnv[$key], "Process")
  }
}

Write-Host "Built chat demo targets in $AbsBuild exit=0"
exit 0
