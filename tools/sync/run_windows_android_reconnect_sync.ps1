# Windows <-> Android Aether chat reconnect synchronization smoke.
#
# Covers three reconnect scenarios on top of one paired state:
#   16 simultaneous restart of both peers,
#   17 message queued while the receiver is offline,
#   18 reply that has to travel back on the incoming stream.
#
# Requires network access for Aether registration / cloud / P2P.
# Does one fresh Android pm clear and a dedicated Windows state-dir, then keeps
# both states across the scenarios on purpose.

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

  [int]$SyncTimeoutSec = 240,

  [int]$WritableTimeoutSec = 240,

  # Observation window for the gated-retry check (needs > 3 retry intervals).
  [int]$GatedObserveSec = 8
)

$ErrorActionPreference = "Stop"

$PackageName = "com.apptraverse.singleclientchat"
$ActivityName = "$PackageName/.MainActivity"
$WindowsClientName = "apptraverse-windows-reconnect-sync"

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
  foreach ($pattern in @("FATAL EXCEPTION", "JNI DETECTED ERROR", "SIGSEGV", "native crash", "assertion failure", "P2P timeout")) {
    if ($Logs -match [regex]::Escape($pattern)) {
      throw "$Label contains crash marker: $pattern"
    }
  }
  Write-Host "  OK  no crash markers in $Label"
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

# Launch without -W so both peers can come up at the same time.
function Start-AppNoWait([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "am", "start", "-n", $ActivityName) | Out-Null
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
    if ($Process.HasExited) {
      $text = if (Test-Path $LogPath) { Get-Content -Raw $LogPath } else { "" }
      throw "Windows process exited early (code=$($Process.ExitCode)) while waiting for $Description`n$text"
    }
    Start-Sleep -Milliseconds 400
  }
  $text = if (Test-Path $LogPath) { Get-Content -Raw $LogPath } else { "" }
  throw "Timed out waiting for Windows $Description`n$text"
}

function Start-WindowsRedirected([string]$Exe, [string]$WorkingDir, [string]$Arguments, [string]$LogPath) {
  $cmd = "cd /d `"$WorkingDir`" && `"$Exe`" $Arguments > `"$LogPath`" 2>&1"
  return Start-Process -FilePath "cmd.exe" -ArgumentList @("/c", $cmd) -PassThru -WindowStyle Hidden
}

function Stop-WindowsChat {
  Get-Process win32_single_client_chat -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
}

function Stop-WindowsRun([System.Diagnostics.Process]$Process) {
  if ($null -ne $Process -and -not $Process.HasExited) {
    Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
  }
  Stop-WindowsChat
  Start-Sleep -Seconds 1
}

function Get-WindowsLog([string]$LogPath) {
  if (-not (Test-Path $LogPath)) {
    return ""
  }
  $text = Get-Content -Raw -Path $LogPath -ErrorAction SilentlyContinue
  if ($null -eq $text) {
    return ""
  }
  return $text
}

function Get-MarkerCount([string]$Text, [string]$Pattern) {
  return ([regex]::Matches($Text, $Pattern)).Count
}

function Get-UiDump([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "uiautomator", "dump", "/sdcard/apptraverse_ui.xml") | Out-Null
  return Invoke-Adb $Adb $DeviceSerial @("shell", "cat", "/sdcard/apptraverse_ui.xml")
}

function Get-UiNode([string]$Xml, [string]$ResourceId) {
  $escaped = [regex]::Escape($ResourceId)
  $match = [regex]::Match($Xml, '<node\b[^>]*resource-id="' + $escaped + '"[^>]*/>')
  if (-not $match.Success) {
    $match = [regex]::Match($Xml, '<node\b[^>]*resource-id="' + $escaped + '"[^>]*>')
  }
  if (-not $match.Success) {
    return $null
  }
  $node = $match.Value
  $text = ""
  if ($node -match '\btext="([^"]*)"') {
    $text = $Matches[1]
  }
  $x = 0
  $y = 0
  if ($node -match 'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"') {
    $x = [int](([int]$Matches[1] + [int]$Matches[3]) / 2)
    $y = [int](([int]$Matches[2] + [int]$Matches[4]) / 2)
  }
  return [pscustomobject]@{ Text = $text; X = $x; Y = $y }
}

function Assert-TranscriptContains([string]$Xml, [string]$Text) {
  $normalized = $Xml -replace '&#10;', ' ' -replace '\s+', ' '
  if ($normalized -notmatch [regex]::Escape($Text)) {
    throw "Transcript UI does not contain '$Text'"
  }
  Write-Host "  OK  transcript contains $Text"
}

function Send-AndroidMessage([string]$Adb, [string]$DeviceSerial, [string]$Text) {
  # Bring chat to foreground without waiting (-W can race with the next UI dump).
  Invoke-Adb $Adb $DeviceSerial @("shell", "am", "start", "-n", $ActivityName) | Out-Null
  Start-Sleep -Seconds 1

  $last_got = "<unset>"
  for ($attempt = 1; $attempt -le 3; $attempt++) {
    $xml = Get-UiDump $Adb $DeviceSerial
    $input = Get-UiNode $xml "$PackageName`:id/message_input"
    if ($null -eq $input -or $input.X -le 0) {
      throw "Unable to locate message_input"
    }
    Invoke-Adb $Adb $DeviceSerial @("shell", "input", "tap", "$($input.X)", "$($input.Y)") | Out-Null
    Start-Sleep -Milliseconds 500
    # Clear any leftover text in one adb round-trip (input text always appends).
    $clear_args = @("shell", "input", "keyevent", "123")
    for ($i = 0; $i -lt 64; $i++) {
      $clear_args += "67"
    }
    Invoke-Adb $Adb $DeviceSerial $clear_args | Out-Null
    Start-Sleep -Milliseconds 300
    # Quote for the device shell so underscores and similar stay intact.
    Invoke-Adb $Adb $DeviceSerial @("shell", "input", "text", $Text) | Out-Null
    Start-Sleep -Milliseconds 700
    $xml = Get-UiDump $Adb $DeviceSerial
    $typed = Get-UiNode $xml "$PackageName`:id/message_input"
    if ($null -ne $typed -and $typed.Text -eq $Text) {
      Invoke-Adb $Adb $DeviceSerial @("shell", "input", "keyevent", "66") | Out-Null
      Write-Host "  OK  Android sent '$Text'"
      return
    }
    $last_got = if ($null -eq $typed) { "<missing>" } else { $typed.Text }
    Write-Host "  WARN Android type attempt $attempt got '$last_got' (want '$Text')"
    Start-Sleep -Seconds 1
  }
  throw "EditText does not contain expected text '$Text' before Send (got '$last_got')"
}

$repo_root = Resolve-RepoRoot
$sdk = Resolve-AndroidSdk
$adb = Get-Tool $sdk "platform-tools\adb.exe"
$android_dir = Join-Path $repo_root "examples\single_client_chat\android"
$out_dir = Join-Path $repo_root "build\reconnect-sync"
$win_state_dir = Join-Path $out_dir "windows-state"
$win_build_dir = Join-Path $repo_root "build-msvc"
$win_exe = Join-Path $win_build_dir "examples\single_client_chat\windows\Debug\win32_single_client_chat.exe"

New-Item -ItemType Directory -Force -Path $out_dir | Out-Null
if (Test-Path $win_state_dir) {
  Remove-Item -Recurse -Force $win_state_dir
}
New-Item -ItemType Directory -Force -Path $win_state_dir | Out-Null

Write-Host "Repository root      : $repo_root"
Write-Host "Output directory     : $out_dir"
Write-Host "Windows state-dir    : $win_state_dir"
Write-Host "Windows client name  : $WindowsClientName"
Write-Host "Selected Android SDK : $sdk"
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

# --- Build Android APK (both ABIs) ---
if (-not $SkipAndroidBuild) {
  Ensure-JavaHome
  Write-Host "JAVA_HOME            : $env:JAVA_HOME"
  $env:ANDROID_SDK_ROOT = $sdk
  $env:ANDROID_HOME = $sdk
  $gradlew = Join-Path $android_dir "gradlew.bat"
  Write-Host "Building Android Debug APK (x86_64 + arm64-v8a)"
  Push-Location $android_dir
  try {
    & cmd /c "`"$gradlew`" -Papptraverse.abiFilters=x86_64,arm64-v8a :app:assembleDebug"
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

# Fresh Android state once; every scenario below reuses it.
Write-Host "pm clear $PackageName"
Invoke-Adb $adb $Serial @("shell", "pm", "clear", $PackageName) | Out-Null

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

Stop-WindowsChat

Write-Host "Distilling Windows state"
& $win_exe --distill --state-dir $win_state_dir --aether-client-name $WindowsClientName
if ($LASTEXITCODE -ne 0) { throw "Windows --distill failed" }

# --- Capture UIDs ---
Write-Host ""
Write-Host "Collecting Aether UIDs"
Stop-App $adb $Serial
Clear-Logcat $adb $Serial
Start-App $adb $Serial
$android_ready = Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=" `
  "Android AETHER_CLIENT_READY" $ClientReadyTimeoutSec
$android_uid = Get-UidFromMarker $android_ready "android"
Wait-Marker $adb $Serial "CHAT_SYNC_CONTROLLER_READY" "Android chat sync ready" 60
Write-Host "  Android UID = $android_uid"

$win_uid_log = Join-Path $out_dir "windows_uid.log"
Remove-Item $win_uid_log -ErrorAction SilentlyContinue
$win_uid_proc = Start-WindowsRedirected $win_exe $repo_root `
  "--state-dir `"$win_state_dir`" --aether-client-name $WindowsClientName --print-aether-uid" `
  $win_uid_log
try {
  $win_ready = Wait-WindowsMarker $win_uid_proc $win_uid_log `
    "AETHER_CLIENT_READY platform=windows uid=" "Windows AETHER_CLIENT_READY" $ClientReadyTimeoutSec
  $windows_uid = Get-UidFromMarker $win_ready "windows"
  Wait-WindowsMarker $win_uid_proc $win_uid_log "AETHER_UID=" "Windows AETHER_UID printed" 30
} finally {
  Stop-WindowsRun $win_uid_proc
}
Write-Host "  Windows UID = $windows_uid"
"$android_uid" | Set-Content (Join-Path $out_dir "android_uid.txt")
"$windows_uid" | Set-Content (Join-Path $out_dir "windows_uid.txt")

# --- Pair once so both sides persist the peer binding ---
Write-Host ""
Write-Host "Setup: first link between Windows and Android"
Stop-App $adb $Serial
Start-Sleep -Seconds 1
Clear-Logcat $adb $Serial
Start-App $adb $Serial
Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=$android_uid" `
  "Android ready for first link" $ClientReadyTimeoutSec
Wait-Marker $adb $Serial "CHAT_SYNC_CONTROLLER_READY" "Android sync controller ready" 60

$win_link_log = Join-Path $out_dir "windows_link.log"
Remove-Item $win_link_log -ErrorAction SilentlyContinue
$win_link_args = @(
  "--state-dir `"$win_state_dir`"",
  "--aether-client-name $WindowsClientName",
  "--peer $android_uid",
  "--auto-accept-peer",
  "--send-after-sync link_hello_windows",
  "--wait-for-message link_hello_android",
  "--exit-after-message"
) -join " "
$win_link = Start-WindowsRedirected $win_exe $repo_root $win_link_args $win_link_log

Wait-WindowsMarker $win_link $win_link_log "CHAT_PEER_ADDED" "Windows CHAT_PEER_ADDED" $SyncTimeoutSec
Wait-Marker $adb $Serial "CHAT_PEER_ADDED" "Android CHAT_PEER_ADDED" $SyncTimeoutSec
Wait-WindowsMarker $win_link $win_link_log "CHAT_SYNC_INITIAL_COMPLETE" `
  "Windows CHAT_SYNC_INITIAL_COMPLETE" $SyncTimeoutSec
Wait-Marker $adb $Serial "CHAT_SYNC_INITIAL_COMPLETE" "Android CHAT_SYNC_INITIAL_COMPLETE" $SyncTimeoutSec
Wait-WindowsMarker $win_link $win_link_log "CHAT_SEND_AFTER_SYNC text=link_hello_windows" `
  "Windows sent link_hello_windows" $SyncTimeoutSec
Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED .*link_hello_windows" `
  "Android saw link_hello_windows" $SyncTimeoutSec

Send-AndroidMessage $adb $Serial "link_hello_android"
Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=link_hello_android" `
  "Android committed link_hello_android" 60
Wait-WindowsMarker $win_link $win_link_log "CHAT_MESSAGE_VISIBLE text=link_hello_android" `
  "Windows saw link_hello_android" $SyncTimeoutSec

$link_deadline = (Get-Date).AddSeconds(60)
while (-not $win_link.HasExited -and (Get-Date) -lt $link_deadline) {
  Start-Sleep -Milliseconds 200
}
if (-not $win_link.HasExited) {
  Stop-WindowsRun $win_link
  throw "Windows first link did not exit after link_hello_android"
}
if ($win_link.ExitCode -ne 0) {
  throw "Windows first link exit code $($win_link.ExitCode)"
}
Write-Host "  OK  Windows first link exit 0"
Stop-WindowsChat

$android_link_logs = Get-Logcat $adb $Serial
$android_link_logs | Set-Content (Join-Path $out_dir "android_link.logcat.txt")
Assert-NoCrash $android_link_logs "Android after first link"
Assert-NoCrash (Get-WindowsLog $win_link_log) "Windows after first link"

# Both states now hold a persisted peer binding.
Stop-App $adb $Serial
Start-Sleep -Seconds 2

# --- Scenario 16: simultaneous restart ---
Write-Host ""
Write-Host "Scenario 16: simultaneous restart of both peers"
Stop-WindowsChat
Stop-App $adb $Serial
Start-Sleep -Seconds 2
Clear-Logcat $adb $Serial

$win_sim_log = Join-Path $out_dir "windows_simultaneous.log"
Remove-Item $win_sim_log -ErrorAction SilentlyContinue
$win_sim_args = @(
  "--state-dir `"$win_state_dir`"",
  "--aether-client-name $WindowsClientName",
  "--send-after-sync simultaneous_windows",
  "--wait-for-message simultaneous_android",
  "--exit-after-message"
) -join " "
# No --peer: the peer binding has to come back from the persisted ChatPeerSet.
$win_sim = Start-WindowsRedirected $win_exe $repo_root $win_sim_args $win_sim_log
Start-AppNoWait $adb $Serial

try {
  Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=$android_uid" `
    "Android ready after simultaneous restart" $ClientReadyTimeoutSec
  Wait-Marker $adb $Serial "CHAT_SYNC_RESUMED .*initial_complete=1" `
    "Android CHAT_SYNC_RESUMED" $SyncTimeoutSec
  $win_sim_ready = Wait-WindowsMarker $win_sim $win_sim_log `
    "AETHER_CLIENT_READY platform=windows uid=" "Windows ready after simultaneous restart" `
    $ClientReadyTimeoutSec
  $windows_uid_sim = Get-UidFromMarker $win_sim_ready "windows"
  if ($windows_uid_sim -ne $windows_uid) {
    throw "Windows UID changed after restart: $windows_uid -> $windows_uid_sim"
  }
  Wait-WindowsMarker $win_sim $win_sim_log "CHAT_SYNC_RESUMED .*initial_complete=1" `
    "Windows CHAT_SYNC_RESUMED" $SyncTimeoutSec

  Wait-WindowsMarker $win_sim $win_sim_log "P2P_PEER_STATE peer=\S+ state=writable" `
    "Windows peer transport writable" $WritableTimeoutSec
  Wait-Marker $adb $Serial "P2P_PEER_STATE peer=\S+ state=writable" `
    "Android peer transport writable" $WritableTimeoutSec

  Wait-WindowsMarker $win_sim $win_sim_log "CHAT_SEND_AFTER_SYNC text=simultaneous_windows" `
    "Windows sent simultaneous_windows" $SyncTimeoutSec
  Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED .*simultaneous_windows" `
    "Android saw simultaneous_windows" $SyncTimeoutSec

  Send-AndroidMessage $adb $Serial "simultaneous_android"
  Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=simultaneous_android" `
    "Android committed simultaneous_android" 60
  Wait-WindowsMarker $win_sim $win_sim_log "CHAT_MESSAGE_VISIBLE text=simultaneous_android" `
    "Windows saw simultaneous_android" $SyncTimeoutSec
} catch {
  Stop-WindowsRun $win_sim
  (Get-Logcat $adb $Serial) | Set-Content (Join-Path $out_dir "android_simultaneous_failure.logcat.txt")
  throw
}

$sim_deadline = (Get-Date).AddSeconds(60)
while (-not $win_sim.HasExited -and (Get-Date) -lt $sim_deadline) {
  Start-Sleep -Milliseconds 200
}
if (-not $win_sim.HasExited) {
  Stop-WindowsRun $win_sim
  throw "Windows simultaneous restart run did not exit"
}
if ($win_sim.ExitCode -ne 0) {
  throw "Windows simultaneous restart exit code $($win_sim.ExitCode)"
}
Write-Host "  OK  Windows simultaneous restart exit 0"
Stop-WindowsChat

$android_sim_logs = Get-Logcat $adb $Serial
$android_sim_logs | Set-Content (Join-Path $out_dir "android_simultaneous.logcat.txt")
Assert-NoCrash $android_sim_logs "Android after simultaneous restart"
Assert-NoCrash (Get-WindowsLog $win_sim_log) "Windows after simultaneous restart"

$ui_sim = Get-UiDump $adb $Serial
$ui_sim | Set-Content (Join-Path $out_dir "android_ui_simultaneous.xml")
Assert-TranscriptContains $ui_sim "simultaneous_windows"
Assert-TranscriptContains $ui_sim "simultaneous_android"
Write-Host "Scenario 16 PASSED"

# --- Scenario 17: message queued while the receiver is offline ---
Write-Host ""
Write-Host "Scenario 17: message committed while Android is stopped"
Stop-App $adb $Serial
Start-Sleep -Seconds 3
Clear-Logcat $adb $Serial

$win_offline_log = Join-Path $out_dir "windows_offline_receiver.log"
Remove-Item $win_offline_log -ErrorAction SilentlyContinue
$win_offline_args = @(
  "--state-dir `"$win_state_dir`"",
  "--aether-client-name $WindowsClientName",
  "--commit-message queued_while_android_stopped"
) -join " "
$win_offline = Start-WindowsRedirected $win_exe $repo_root $win_offline_args $win_offline_log

try {
  Wait-WindowsMarker $win_offline $win_offline_log "CHAT_SYNC_RESUMED .*initial_complete=1" `
    "Windows CHAT_SYNC_RESUMED with Android down" $ClientReadyTimeoutSec
  Wait-WindowsMarker $win_offline $win_offline_log `
    "MESSAGE_COMMITTED text=queued_while_android_stopped" `
    "Windows committed queued_while_android_stopped" 120

  # Aether may keep a cloud path "writable" even when the Android app is
  # force-stopped. Prefer CHAT_RETRY_GATED when the local transport is not
  # writable; otherwise require rate-limited CHAT_RETRY_SENT with pending held.
  Start-Sleep -Seconds $GatedObserveSec
  $offline_text = Get-WindowsLog $win_offline_log
  if ($offline_text -match "CHAT_PENDING_CLEARED") {
    throw "Pending cleared while Android was stopped"
  }
  $gated_count = Get-MarkerCount $offline_text "CHAT_RETRY_GATED peer=\S+ pending=\d+"
  $sent_count = Get-MarkerCount $offline_text "CHAT_RETRY_SENT peer=\S+ pending=\d+"
  $budget = 2 * $GatedObserveSec + 10
  if ($gated_count -ge 3) {
    if ($gated_count -gt $budget) {
      throw "CHAT_RETRY_GATED flooded: $gated_count markers (budget $budget)"
    }
    Write-Host "  OK  $gated_count gated retries over ~$GatedObserveSec s (budget $budget)"
    $last = ([regex]::Matches($offline_text, "CHAT_RETRY_GATED peer=\S+ pending=(\d+)"))
    $last_pending = [int]$last[$last.Count - 1].Groups[1].Value
  } elseif ($sent_count -ge 1) {
    if ($sent_count -gt $budget) {
      throw "CHAT_RETRY_SENT flooded: $sent_count markers (budget $budget)"
    }
    Write-Host "  OK  $sent_count retry sends over ~$GatedObserveSec s while Android stopped (transport stayed writable)"
    $last = ([regex]::Matches($offline_text, "CHAT_RETRY_SENT peer=\S+ pending=(\d+)"))
    $last_pending = [int]$last[$last.Count - 1].Groups[1].Value
  } else {
    throw "Expected CHAT_RETRY_GATED or CHAT_RETRY_SENT while Android is stopped (gated=$gated_count sent=$sent_count)"
  }
  if ($last_pending -lt 1) {
    throw "Expected pending packets while Android is stopped, got $last_pending"
  }
  Write-Host "  OK  pending stays at $last_pending while Android is stopped"


  Write-Host "  Starting Android again"
  Start-App $adb $Serial
  Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=$android_uid" `
    "Android ready after outage" $ClientReadyTimeoutSec
  Wait-Marker $adb $Serial "CHAT_SYNC_RESUMED .*initial_complete=1" `
    "Android CHAT_SYNC_RESUMED after outage" $SyncTimeoutSec
  Wait-WindowsMarker $win_offline $win_offline_log "P2P_PEER_STATE peer=\S+ state=writable" `
    "Windows peer writable after outage" $WritableTimeoutSec
  Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED .*queued_while_android_stopped" `
    "Android received the queued message" $SyncTimeoutSec
  Wait-WindowsMarker $win_offline $win_offline_log "CHAT_PENDING_CLEARED" `
    "Windows pending cleared by the ACK" $SyncTimeoutSec
} catch {
  Stop-WindowsRun $win_offline
  (Get-Logcat $adb $Serial) | Set-Content (Join-Path $out_dir "android_offline_failure.logcat.txt")
  throw
}

$ui_offline = Get-UiDump $adb $Serial
$ui_offline | Set-Content (Join-Path $out_dir "android_ui_offline_receiver.xml")
Assert-TranscriptContains $ui_offline "queued_while_android_stopped"

$android_offline_logs = Get-Logcat $adb $Serial
$android_offline_logs | Set-Content (Join-Path $out_dir "android_offline_receiver.logcat.txt")
Assert-NoCrash $android_offline_logs "Android after offline receiver scenario"
Assert-NoCrash (Get-WindowsLog $win_offline_log) "Windows after offline receiver scenario"
Stop-WindowsRun $win_offline
Write-Host "Scenario 17 PASSED"

# --- Scenario 18: reply travels back on the incoming stream ---
Write-Host ""
Write-Host "Scenario 18: Android replies on the incoming stream"
Stop-WindowsChat
Stop-App $adb $Serial
Start-Sleep -Seconds 2
Clear-Logcat $adb $Serial
# Android comes up alone: it never dials out, so its only stream is the one
# Windows creates below.
Start-App $adb $Serial
Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=$android_uid" `
  "Android ready as passive listener" $ClientReadyTimeoutSec
Wait-Marker $adb $Serial "CHAT_SYNC_RESUMED .*initial_complete=1" `
  "Android CHAT_SYNC_RESUMED as listener" $SyncTimeoutSec
Start-Sleep -Seconds 3

$win_incoming_log = Join-Path $out_dir "windows_incoming_reply.log"
Remove-Item $win_incoming_log -ErrorAction SilentlyContinue
$win_incoming_args = @(
  "--state-dir `"$win_state_dir`"",
  "--aether-client-name $WindowsClientName",
  "--send-after-sync reply_probe_windows"
) -join " "
$win_incoming = Start-WindowsRedirected $win_exe $repo_root $win_incoming_args $win_incoming_log

try {
  Wait-WindowsMarker $win_incoming $win_incoming_log "CHAT_SYNC_RESUMED .*initial_complete=1" `
    "Windows CHAT_SYNC_RESUMED for incoming reply" $ClientReadyTimeoutSec
  Wait-WindowsMarker $win_incoming $win_incoming_log "P2P_PEER_STATE peer=\S+ state=writable" `
    "Windows peer writable for incoming reply" $WritableTimeoutSec
  Wait-WindowsMarker $win_incoming $win_incoming_log "CHAT_SEND_AFTER_SYNC text=reply_probe_windows" `
    "Windows sent reply_probe_windows" $SyncTimeoutSec
  Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED .*reply_probe_windows" `
    "Android received reply_probe_windows" $SyncTimeoutSec
  # The ACK has to go out on the stream the packet arrived on.
  Wait-Marker $adb $Serial "P2P_STREAM_SELECTED peer=\S+ direction=incoming reason=recent_receive" `
    "Android replied on the incoming stream" $SyncTimeoutSec
  Wait-WindowsMarker $win_incoming $win_incoming_log "CHAT_PENDING_CLEARED" `
    "Windows pending cleared by the incoming-stream ACK" $SyncTimeoutSec
} catch {
  Stop-WindowsRun $win_incoming
  (Get-Logcat $adb $Serial) | Set-Content (Join-Path $out_dir "android_incoming_failure.logcat.txt")
  throw
}

$ui_incoming = Get-UiDump $adb $Serial
$ui_incoming | Set-Content (Join-Path $out_dir "android_ui_incoming_reply.xml")
Assert-TranscriptContains $ui_incoming "reply_probe_windows"

$android_incoming_logs = Get-Logcat $adb $Serial
$android_incoming_logs | Set-Content (Join-Path $out_dir "android_incoming_reply.logcat.txt")
Assert-NoCrash $android_incoming_logs "Android after incoming reply scenario"
Assert-NoCrash (Get-WindowsLog $win_incoming_log) "Windows after incoming reply scenario"
Stop-WindowsRun $win_incoming
Write-Host "Scenario 18 PASSED"

# --- Everything survived the reconnects ---
Write-Host ""
Write-Host "Final transcript check"
$ui_final = Get-UiDump $adb $Serial
$ui_final | Set-Content (Join-Path $out_dir "android_ui_final.xml")
foreach ($msg in @("link_hello_windows", "link_hello_android", "simultaneous_windows",
                   "simultaneous_android", "queued_while_android_stopped",
                   "reply_probe_windows")) {
  Assert-TranscriptContains $ui_final $msg
}

Write-Host ""
Write-Host "Reconnect sync smoke PASSED"
Write-Host "Android UID : $android_uid"
Write-Host "Windows UID : $windows_uid"
Write-Host "Logs        : $out_dir"
