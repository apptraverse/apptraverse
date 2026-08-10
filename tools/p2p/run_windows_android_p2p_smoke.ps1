# End-to-end Windows <-> Android Aether P2P ping/pong smoke.
#
# Requires network access for Aether registration / cloud / P2P.
# Does not pm clear after the test. Does not send Chat messages.

[CmdletBinding()]
param(
  [string]$Serial = "",

  [string]$ApkPath = "",

  [ValidateSet("x86_64", "arm64-v8a")]
  [string]$Abi = "x86_64",

  [switch]$SkipAndroidBuild,

  [switch]$SkipWindowsBuild,

  [switch]$SkipInstall,

  [int]$ClientReadyTimeoutSec = 180,

  [int]$PongTimeoutSec = 120
)

$ErrorActionPreference = "Stop"

$PackageName = "com.apptraverse.singleclientchat"
$ActivityName = "$PackageName/.MainActivity"

function Resolve-RepoRoot {
  $script_dir = Split-Path -Parent $PSCommandPath
  return (Resolve-Path (Join-Path $script_dir "..\..")).Path
}

function Resolve-AndroidSdk {
  if ($env:ANDROID_SDK_ROOT -and (Test-Path $env:ANDROID_SDK_ROOT)) {
    return (Resolve-Path $env:ANDROID_SDK_ROOT).Path
  }
  if ($env:ANDROID_HOME -and (Test-Path $env:ANDROID_HOME)) {
    return (Resolve-Path $env:ANDROID_HOME).Path
  }
  $default = Join-Path $env:LOCALAPPDATA "Android\Sdk"
  if (Test-Path $default) {
    return (Resolve-Path $default).Path
  }
  throw "Android SDK not found. Set ANDROID_SDK_ROOT or install the Android SDK."
}

function Get-Tool([string]$Sdk, [string]$Relative) {
  $path = Join-Path $Sdk $Relative
  if (-not (Test-Path $path)) {
    throw "Required tool not found: $path"
  }
  return $path
}

function Get-AdbDevices([string]$Adb) {
  $lines = & $Adb devices | Select-Object -Skip 1
  $devices = @()
  foreach ($line in $lines) {
    if ($line -match "^\s*$") { continue }
    if ($line -match "^(\S+)\s+device\s*$") {
      $devices += $Matches[1]
    }
  }
  return $devices
}

function Invoke-Adb([string]$Adb, [string]$DeviceSerial, [string[]]$Arguments) {
  $prev = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  $output = & $Adb -s $DeviceSerial @Arguments 2>&1 | ForEach-Object { "$_" } | Out-String
  $code = $LASTEXITCODE
  $ErrorActionPreference = $prev
  if ($code -ne 0) {
    throw "adb $($Arguments -join ' ') failed with exit code $code`n$output"
  }
  return $output
}

function Clear-Logcat([string]$Adb, [string]$DeviceSerial) {
  $prev = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  & $Adb -s $DeviceSerial logcat -c 2>&1 | Out-Null
  $ErrorActionPreference = $prev
}

function Get-Logcat([string]$Adb, [string]$DeviceSerial) {
  $prev = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  $logs = (& $Adb -s $DeviceSerial logcat -d -v brief 2>&1 | ForEach-Object { "$_" } | Out-String)
  $ErrorActionPreference = $prev
  return $logs
}

function Wait-Marker(
    [string]$Adb, [string]$DeviceSerial, [string]$Pattern, [string]$Description,
    [int]$TimeoutSec = 90) {
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  $logs = ""
  while ((Get-Date) -lt $deadline) {
    $logs = Get-Logcat $Adb $DeviceSerial
    $match = ($logs -split "`n" | Where-Object { $_ -match $Pattern } | Select-Object -First 1)
    if ($match) {
      Write-Host "  OK  $Description"
      return $match.Trim()
    }
    Start-Sleep -Milliseconds 500
  }
  Write-Host "----- logcat -----"
  Write-Host $logs
  Write-Host "----- end logcat -----"
  throw "Timed out waiting for $Description (pattern: $Pattern)"
}

function Assert-NoCrash([string]$Logs, [string]$Label) {
  foreach ($pattern in @("FATAL EXCEPTION", "JNI DETECTED ERROR", "SIGSEGV", "native crash")) {
    if ($Logs -match [regex]::Escape($pattern)) {
      throw "$Label contains crash marker: $pattern"
    }
  }
  Write-Host "  OK  no crash markers in $Label"
}

function Get-LatestChatJournalSize([string]$Logs) {
  $matches = [regex]::Matches($Logs, "CHAT_JOURNAL_SIZE n=(\d+)")
  if ($matches.Count -eq 0) {
    throw "No CHAT_JOURNAL_SIZE marker found"
  }
  return [int]$matches[$matches.Count - 1].Groups[1].Value
}

function Get-UidFromMarker([string]$Line, [string]$Platform) {
  if ($Line -notmatch "AETHER_CLIENT_READY platform=$Platform uid=([0-9a-fA-F-]+)") {
    throw "Unable to parse $Platform UID from: $Line"
  }
  return $Matches[1]
}

function Start-App([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "am", "start", "-W", "-n", $ActivityName) | Out-Null
}

function Stop-App([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "am", "force-stop", $PackageName) | Out-Null
}

function Ensure-JavaHome {
  if ($env:JAVA_HOME -and (Test-Path $env:JAVA_HOME)) {
    return
  }
  $jdk20 = "C:\Program Files\Java\jdk-20"
  $jbr = Join-Path ${env:ProgramFiles} "Android\Android Studio\jbr"
  if (Test-Path $jdk20) {
    $env:JAVA_HOME = $jdk20
  } elseif (Test-Path $jbr) {
    $env:JAVA_HOME = $jbr
  } else {
    throw "No JDK found. Set JAVA_HOME to JDK 17-23."
  }
}

function Wait-WindowsMarker(
    [System.Diagnostics.Process]$Process,
    [string]$LogPath,
    [string]$Pattern,
    [string]$Description,
    [int]$TimeoutSec) {
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
    if ($Process.HasExited -and $Pattern -notmatch "P2P_PONG") {
      $text = if (Test-Path $LogPath) { Get-Content -Raw $LogPath } else { "" }
      throw "Windows process exited early (code=$($Process.ExitCode)) while waiting for $Description`n$text"
    }
    Start-Sleep -Milliseconds 400
  }
  $text = if (Test-Path $LogPath) { Get-Content -Raw $LogPath } else { "" }
  throw "Timed out waiting for Windows $Description`n$text"
}

$repo_root = Resolve-RepoRoot
$sdk = Resolve-AndroidSdk
$adb = Get-Tool $sdk "platform-tools\adb.exe"
$android_dir = Join-Path $repo_root "examples\single_client_chat\android"
$out_dir = Join-Path $repo_root "build\p2p-smoke"
$win_state_dir = Join-Path $out_dir "windows-state"
$win_build_dir = Join-Path $repo_root "build"
$win_exe = Join-Path $win_build_dir "examples\single_client_chat\windows\Debug\win32_single_client_chat.exe"

New-Item -ItemType Directory -Force -Path $out_dir | Out-Null
New-Item -ItemType Directory -Force -Path $win_state_dir | Out-Null

Write-Host "Repository root      : $repo_root"
Write-Host "Output directory     : $out_dir"
Write-Host "Selected Android SDK : $sdk"
Write-Host "Package              : $PackageName"
Write-Host "ABI                  : $Abi"

if (-not $ApkPath) {
  $ApkPath = Join-Path $android_dir "app\build\outputs\apk\debug\app-debug.apk"
}

if (-not $env:CPM_SOURCE_CACHE) {
  $env:CPM_SOURCE_CACHE = "C:\cpm-cache"
}

# --- Android device / emulator ---
$devices = Get-AdbDevices $adb
if ($Serial) {
  if ($devices -notcontains $Serial) {
    throw "Requested device '$Serial' is not connected"
  }
} else {
  $emulator = Get-Tool $sdk "emulator\emulator.exe"
  $preferred = "Aether_NDK_Smoke_x86_64"
  $serial_found = $null
  foreach ($d in $devices) {
    $runtime_abi = (& $adb -s $d shell getprop ro.product.cpu.abi).Trim()
    if ($runtime_abi -eq "x86_64") {
      $serial_found = $d
      break
    }
  }
  if (-not $serial_found) {
    Write-Host "Starting emulator $preferred"
    Start-Process -FilePath $emulator -ArgumentList @("-avd", $preferred, "-no-snapshot-save", "-no-boot-anim") | Out-Null
    $deadline = (Get-Date).AddSeconds(300)
    while ((Get-Date) -lt $deadline -and -not $serial_found) {
      Start-Sleep -Seconds 3
      foreach ($d in @(Get-AdbDevices $adb)) {
        $runtime_abi = (& $adb -s $d shell getprop ro.product.cpu.abi).Trim()
        if ($runtime_abi -eq "x86_64") {
          $serial_found = $d
          break
        }
      }
    }
    if (-not $serial_found) {
      throw "Failed to start x86_64 emulator"
    }
    & $adb -s $serial_found wait-for-device | Out-Null
    $boot_deadline = (Get-Date).AddSeconds(300)
    while ((Get-Date) -lt $boot_deadline) {
      $boot = (& $adb -s $serial_found shell getprop sys.boot_completed).Trim()
      if ($boot -eq "1") { break }
      Start-Sleep -Seconds 2
    }
  }
  $Serial = $serial_found
}

Write-Host "Device serial        : $Serial"

# --- Build Android APK ---
if (-not $SkipAndroidBuild) {
  Ensure-JavaHome
  Write-Host "JAVA_HOME            : $env:JAVA_HOME"
  $env:ANDROID_SDK_ROOT = $sdk
  $env:ANDROID_HOME = $sdk
  $gradlew = Join-Path $android_dir "gradlew.bat"
  Write-Host "Building Android Debug APK ($Abi)"
  Push-Location $android_dir
  try {
    & cmd /c "`"$gradlew`" -Papptraverse.abiFilters=$Abi :app:assembleDebug"
    if ($LASTEXITCODE -ne 0) {
      throw "Gradle assembleDebug failed with exit code $LASTEXITCODE"
    }
  } finally {
    Pop-Location
  }
}

if (-not (Test-Path $ApkPath)) {
  throw "APK not found: $ApkPath"
}

if (-not $SkipInstall) {
  Write-Host "Installing APK"
  Invoke-Adb $adb $Serial @("install", "-r", "-t", "-g", $ApkPath) | Out-Null
}

# --- Build Windows ---
if (-not $SkipWindowsBuild) {
  Write-Host "Configuring / building Windows Debug"
  if (-not (Test-Path (Join-Path $win_build_dir "CMakeCache.txt"))) {
    cmake -S $repo_root -B $win_build_dir -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
  }
  cmake --build $win_build_dir --config Debug --target win32_single_client_chat
  if ($LASTEXITCODE -ne 0) { throw "Windows build failed" }
}

if (-not (Test-Path $win_exe)) {
  throw "Windows executable not found: $win_exe"
}

# Distill Windows state if missing.
$win_state_marker = Join-Path $win_state_dir "state"
if (-not (Test-Path $win_state_marker)) {
  Write-Host "Distilling Windows state into $win_state_dir"
  Push-Location $win_state_dir
  try {
    & $win_exe --distill
    if ($LASTEXITCODE -ne 0) { throw "Windows --distill failed" }
  } finally {
    Pop-Location
  }
}

# --- Android UID restart check ---
Write-Host ""
Write-Host "Android UID restart check"
Stop-App $adb $Serial
Clear-Logcat $adb $Serial
Start-App $adb $Serial
$android_ready1 = Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=" `
  "Android AETHER_CLIENT_READY #1" $ClientReadyTimeoutSec
$android_uid1 = Get-UidFromMarker $android_ready1 "android"
Write-Host "  Android UID #1 = $android_uid1"
Wait-Marker $adb $Serial "AETHER_P2P_TRANSPORT_READY" "Android P2P transport ready" 60
$android_before_logs = Get-Logcat $adb $Serial
Assert-NoCrash $android_before_logs "Android startup log"
# Force a journal size marker if missing by relying on PublishTranscript path.
if ($android_before_logs -notmatch "CHAT_JOURNAL_SIZE") {
  Write-Host "  Waiting for CHAT_JOURNAL_SIZE after transcript publish"
  Wait-Marker $adb $Serial "CHAT_JOURNAL_SIZE n=" "Android CHAT_JOURNAL_SIZE" 60
  $android_before_logs = Get-Logcat $adb $Serial
}
$android_chat_before = Get-LatestChatJournalSize $android_before_logs
Write-Host "  Android CHAT_JOURNAL_SIZE before = $android_chat_before"

Stop-App $adb $Serial
Start-Sleep -Seconds 2
Clear-Logcat $adb $Serial
Start-App $adb $Serial
$android_ready2 = Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=" `
  "Android AETHER_CLIENT_READY #2" $ClientReadyTimeoutSec
$android_uid2 = Get-UidFromMarker $android_ready2 "android"
if ($android_uid2 -ne $android_uid1) {
  throw "Android UID changed after restart: $android_uid1 -> $android_uid2"
}
Write-Host "  OK  Android UID persisted across restart"
Wait-Marker $adb $Serial "AETHER_P2P_TRANSPORT_READY" "Android P2P transport ready after restart" 60
$android_uid = $android_uid2

# --- Windows UID restart check ---
Write-Host ""
Write-Host "Windows UID restart check"
$win_uid_log1 = Join-Path $out_dir "windows_uid1.log"
$win_uid_log2 = Join-Path $out_dir "windows_uid2.log"
Remove-Item $win_uid_log1, $win_uid_log2 -ErrorAction SilentlyContinue

function Start-WindowsChat([string]$LogPath) {
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $win_exe
  $psi.WorkingDirectory = $win_state_dir
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true
  $proc = New-Object System.Diagnostics.Process
  $proc.StartInfo = $psi
  $null = $proc.Start()
  Start-Job -ScriptBlock {
    param($p, $path)
    $p.StandardOutput.ReadToEnd() + $p.StandardError.ReadToEnd() | Set-Content -Path $path
  } -ArgumentList $proc, $LogPath | Out-Null
  # Prefer streaming append:
  return $proc
}

# Simpler streaming via cmd redirection
function Start-WindowsRedirected([string]$Arguments, [string]$LogPath) {
  $cmd = "cd /d `"$win_state_dir`" && `"$win_exe`" $Arguments > `"$LogPath`" 2>&1"
  $proc = Start-Process -FilePath "cmd.exe" -ArgumentList @("/c", $cmd) -PassThru -WindowStyle Hidden
  return $proc
}

$win_proc1 = Start-WindowsRedirected "" $win_uid_log1
try {
  $win_ready1 = Wait-WindowsMarker $win_proc1 $win_uid_log1 `
    "AETHER_CLIENT_READY platform=windows uid=" "Windows AETHER_CLIENT_READY #1" $ClientReadyTimeoutSec
  $windows_uid1 = Get-UidFromMarker $win_ready1 "windows"
  Write-Host "  Windows UID #1 = $windows_uid1"
} finally {
  if (-not $win_proc1.HasExited) {
    Stop-Process -Id $win_proc1.Id -Force -ErrorAction SilentlyContinue
    # Also kill the child exe if cmd wrapper remains
    Get-Process win32_single_client_chat -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  }
}
Start-Sleep -Seconds 2

$win_proc2 = Start-WindowsRedirected "" $win_uid_log2
try {
  $win_ready2 = Wait-WindowsMarker $win_proc2 $win_uid_log2 `
    "AETHER_CLIENT_READY platform=windows uid=" "Windows AETHER_CLIENT_READY #2" $ClientReadyTimeoutSec
  $windows_uid2 = Get-UidFromMarker $win_ready2 "windows"
  if ($windows_uid2 -ne $windows_uid1) {
    throw "Windows UID changed after restart: $windows_uid1 -> $windows_uid2"
  }
  Write-Host "  OK  Windows UID persisted across restart"
} finally {
  if (-not $win_proc2.HasExited) {
    Stop-Process -Id $win_proc2.Id -Force -ErrorAction SilentlyContinue
    Get-Process win32_single_client_chat -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  }
}
$windows_uid = $windows_uid2

# Capture Windows chat journal size before ping via a short launch.
$win_pre_log = Join-Path $out_dir "windows_pre_ping.log"
$win_pre = Start-WindowsRedirected "" $win_pre_log
try {
  Wait-WindowsMarker $win_pre $win_pre_log "CHAT_JOURNAL_SIZE n=" "Windows CHAT_JOURNAL_SIZE before" $ClientReadyTimeoutSec
  $windows_chat_before = Get-LatestChatJournalSize (Get-Content -Raw $win_pre_log)
  Write-Host "  Windows CHAT_JOURNAL_SIZE before = $windows_chat_before"
} finally {
  if (-not $win_pre.HasExited) {
    Stop-Process -Id $win_pre.Id -Force -ErrorAction SilentlyContinue
    Get-Process win32_single_client_chat -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  }
}

# Ensure Android is listening for the ping.
Clear-Logcat $adb $Serial
# App may still be running from restart check; force clean listener start.
Stop-App $adb $Serial
Start-Sleep -Seconds 1
Start-App $adb $Serial
Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=$android_uid" `
  "Android ready for ping" $ClientReadyTimeoutSec
Wait-Marker $adb $Serial "AETHER_P2P_TRANSPORT_READY" "Android P2P listening" 60
$android_chat_before = Get-LatestChatJournalSize (Get-Logcat $adb $Serial)
Write-Host "  Android CHAT_JOURNAL_SIZE before ping = $android_chat_before"

# --- Real P2P ping ---
Write-Host ""
Write-Host "Windows -> Android P2P ping"
$win_ping_log = Join-Path $out_dir "windows_p2p_ping.log"
Remove-Item $win_ping_log -ErrorAction SilentlyContinue
$win_ping = Start-WindowsRedirected "--p2p-ping $android_uid" $win_ping_log

Wait-WindowsMarker $win_ping $win_ping_log "AETHER_CLIENT_READY platform=windows" `
  "Windows client ready for ping" $ClientReadyTimeoutSec
Wait-WindowsMarker $win_ping $win_ping_log "P2P_PING_SENT" "P2P_PING_SENT" $PongTimeoutSec
Wait-Marker $adb $Serial "P2P_PING_RECEIVED" "Android P2P_PING_RECEIVED" $PongTimeoutSec
Wait-Marker $adb $Serial "P2P_PONG_SENT" "Android P2P_PONG_SENT" $PongTimeoutSec
Wait-WindowsMarker $win_ping $win_ping_log "P2P_PONG_RECEIVED" "Windows P2P_PONG_RECEIVED" $PongTimeoutSec

$ping_deadline = (Get-Date).AddSeconds(30)
while (-not $win_ping.HasExited -and (Get-Date) -lt $ping_deadline) {
  Start-Sleep -Milliseconds 200
}
if (-not $win_ping.HasExited) {
  # Child may still be exiting; wait a bit more on the exe
  $exe = Get-Process win32_single_client_chat -ErrorAction SilentlyContinue
  if ($exe) {
    $exe.WaitForExit(15000) | Out-Null
  }
  if (-not $win_ping.HasExited) {
    Stop-Process -Id $win_ping.Id -Force -ErrorAction SilentlyContinue
    throw "Windows --p2p-ping did not exit after PONG"
  }
}

# Exit code of cmd wrapper reflects the exe when using cmd /c
$win_exit = $win_ping.ExitCode
$win_ping_text = Get-Content -Raw $win_ping_log
$win_ping_text | Set-Content (Join-Path $out_dir "windows_p2p_ping_final.log")
if ($win_exit -ne 0) {
  throw "Windows --p2p-ping exit code $win_exit (expected 0)`n$win_ping_text"
}
Write-Host "  OK  Windows exit code 0"

$android_after_logs = Get-Logcat $adb $Serial
$android_after_logs | Set-Content (Join-Path $out_dir "android_after_ping.logcat.txt")
Assert-NoCrash $android_after_logs "Android after ping"
Assert-NoCrash $win_ping_text "Windows after ping"

if ($android_after_logs -notmatch "AETHER_CLIENT_READY platform=android") {
  throw "Missing Android AETHER_CLIENT_READY after ping phase"
}
if ($android_after_logs -notmatch "P2P_PING_RECEIVED") {
  throw "Missing Android P2P_PING_RECEIVED"
}
if ($android_after_logs -notmatch "P2P_PONG_SENT") {
  throw "Missing Android P2P_PONG_SENT"
}
if ($win_ping_text -notmatch "P2P_PING_SENT") {
  throw "Missing Windows P2P_PING_SENT"
}
if ($win_ping_text -notmatch "P2P_PONG_RECEIVED") {
  throw "Missing Windows P2P_PONG_RECEIVED"
}

$android_chat_after = Get-LatestChatJournalSize $android_after_logs
$windows_chat_after = Get-LatestChatJournalSize $win_ping_text
if ($android_chat_after -ne $android_chat_before) {
  throw "Android Chat journal changed: $android_chat_before -> $android_chat_after"
}
if ($windows_chat_after -ne $windows_chat_before) {
  throw "Windows Chat journal changed: $windows_chat_before -> $windows_chat_after"
}
Write-Host "  OK  Chat journals unchanged (android=$android_chat_after windows=$windows_chat_after)"

foreach ($probe in @("APPTRAVERSE_P2P_PING_V1", "APPTRAVERSE_P2P_PONG_V1")) {
  if ($android_after_logs -match "TRANSCRIPT_PUBLISHED.*$probe") {
    throw "Probe payload appeared in Android transcript: $probe"
  }
  if ($win_ping_text -match $probe -and $win_ping_text -match "TRANSCRIPT") {
    throw "Probe payload appeared in Windows transcript path"
  }
}
# Probe strings may appear only in dedicated P2P markers / Send buffers logs — forbid transcript publish.
if ($android_after_logs -match "TRANSCRIPT_PUBLISHED .*APPTRAVERSE_P2P_") {
  throw "PING/PONG leaked into Android TRANSCRIPT_PUBLISHED"
}
Write-Host "  OK  transcript has no PING/PONG payloads"

# Leave Android running for manual re-test.
Write-Host ""
Write-Host "P2P smoke PASSED"
Write-Host "Android UID : $android_uid"
Write-Host "Windows UID : $windows_uid"
Write-Host "Manual retry: `"$win_exe`" --p2p-ping $android_uid"
Write-Host "  (cwd: $win_state_dir)"
Write-Host "Logs: $out_dir"
