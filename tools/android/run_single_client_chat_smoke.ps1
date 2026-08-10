# Persistence smoke for the Android single-client chat example.
#
# Scenario:
#   1. pm clear, launch, expect a freshly created graph
#   2. send hello_android, expect MESSAGE_COMMITTED and STATE_SAVED
#   3. force-stop and relaunch, expect a loaded graph with hello_android
#   4. rotate, send after_rotation, expect both messages in the transcript
#   5. no FATAL EXCEPTION and no JNI errors during the whole session

[CmdletBinding()]
param(
  [string]$Serial = "",

  [string]$ApkPath = "",

  [ValidateSet("x86_64", "arm64-v8a")]
  [string]$Abi = "x86_64",

  [switch]$SkipBuild,

  [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"

$PackageName = "com.apptraverse.singleclientchat"
$ActivityName = "$PackageName/.MainActivity"
$FirstMessage = "hello_android"
$SecondMessage = "after_rotation"

$script:CollectedLogs = New-Object System.Collections.Generic.List[string]

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
  $script:CollectedLogs.Add($logs)
  Write-Host "----- logcat -----"
  Write-Host $logs
  Write-Host "----- end logcat -----"
  throw "Timed out waiting for $Description (pattern: $Pattern)"
}

function Assert-NoMarker([string]$Logs, [string]$Pattern, [string]$Description) {
  if ($Logs -match $Pattern) {
    throw "Unexpected $Description found in logcat"
  }
  Write-Host "  OK  no $Description"
}

function Save-Phase([string]$Adb, [string]$DeviceSerial, [string]$Phase) {
  $logs = Get-Logcat $Adb $DeviceSerial
  $script:CollectedLogs.Add("===== $Phase =====")
  $script:CollectedLogs.Add($logs)
  return $logs
}

function Start-App([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "am", "start", "-W", "-n", $ActivityName) | Out-Null
}

function Stop-App([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "am", "force-stop", $PackageName) | Out-Null
}

function Send-Message([string]$Adb, [string]$DeviceSerial, [string]$Text) {
  Start-Sleep -Seconds 1
  Invoke-Adb $Adb $DeviceSerial @("shell", "uiautomator", "dump", "/sdcard/apptraverse_ui.xml") | Out-Null
  $xml = Invoke-Adb $Adb $DeviceSerial @("shell", "cat", "/sdcard/apptraverse_ui.xml")
  if ($xml -match 'resource-id="com\.apptraverse\.singleclientchat:id/message_input"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"') {
    $x = [int](([int]$Matches[1] + [int]$Matches[3]) / 2)
    $y = [int](([int]$Matches[2] + [int]$Matches[4]) / 2)
    Invoke-Adb $Adb $DeviceSerial @("shell", "input", "tap", "$x", "$y") | Out-Null
  } else {
    throw "Unable to locate message_input"
  }
  Start-Sleep -Milliseconds 400
  # Clear the field with one device-side loop (adb input text always appends).
  Invoke-Adb $Adb $DeviceSerial @(
    "shell",
    "sh",
    "-c",
    "input keyevent 123; i=0; while [ `$i -lt 80 ]; do input keyevent 67; i=`$((i+1)); done") | Out-Null
  Start-Sleep -Milliseconds 300
  Invoke-Adb $Adb $DeviceSerial @("shell", "input", "text", $Text) | Out-Null
  Start-Sleep -Milliseconds 500
  Invoke-Adb $Adb $DeviceSerial @("shell", "uiautomator", "dump", "/sdcard/apptraverse_ui.xml") | Out-Null
  $xml = Invoke-Adb $Adb $DeviceSerial @("shell", "cat", "/sdcard/apptraverse_ui.xml")
  if ($xml -notmatch ('resource-id="com\.apptraverse\.singleclientchat:id/message_input"[^>]*text="' + [regex]::Escape($Text) + '"')) {
    throw "EditText does not contain expected text '$Text' before Send"
  }
  if ($xml -match 'resource-id="com\.apptraverse\.singleclientchat:id/send"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"') {
    $x = [int](([int]$Matches[1] + [int]$Matches[3]) / 2)
    $y = [int](([int]$Matches[2] + [int]$Matches[4]) / 2)
    Invoke-Adb $Adb $DeviceSerial @("shell", "input", "tap", "$x", "$y") | Out-Null
    return
  }
  throw "Unable to locate send button for input"
}

function Set-Rotation([string]$Adb, [string]$DeviceSerial, [int]$Rotation) {
  Invoke-Adb $Adb $DeviceSerial @(
    "shell", "settings", "put", "system", "accelerometer_rotation", "0") | Out-Null
  Invoke-Adb $Adb $DeviceSerial @(
    "shell", "settings", "put", "system", "user_rotation", "$Rotation") | Out-Null
  Start-Sleep -Seconds 3
}

$repo_root = Resolve-RepoRoot
$sdk = Resolve-AndroidSdk
$adb = Get-Tool $sdk "platform-tools\adb.exe"
$android_dir = Join-Path $repo_root "examples\single_client_chat\android"

Write-Host "Repository root      : $repo_root"
Write-Host "Selected Android SDK : $sdk"
Write-Host "Package              : $PackageName"
Write-Host "ABI                  : $Abi"

if (-not $ApkPath) {
  $ApkPath = Join-Path $android_dir "app\build\outputs\apk\debug\app-debug.apk"
}

if (-not $SkipBuild) {
  $gradlew = Join-Path $android_dir "gradlew.bat"
  if (-not (Test-Path $gradlew)) {
    throw "Gradle wrapper not found: $gradlew"
  }
  Write-Host "Building the debug APK for $Abi"
  if (-not $env:JAVA_HOME) {
    $jbr = Join-Path ${env:ProgramFiles} "Android\Android Studio\jbr"
    if (Test-Path $jbr) {
      $env:JAVA_HOME = $jbr
    } else {
      $jdk = Get-ChildItem (Join-Path ${env:ProgramFiles} "Java") -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName "bin\javac.exe") } |
        Sort-Object Name -Descending |
        Select-Object -First 1
      if (-not $jdk) {
        throw "No JDK found. Set JAVA_HOME to the Android Studio JBR or a JDK 17+."
      }
      $env:JAVA_HOME = $jdk.FullName
    }
  }
  Write-Host "JAVA_HOME            : $env:JAVA_HOME"
  $env:ANDROID_SDK_ROOT = $sdk
  $env:ANDROID_HOME = $sdk
  # Keep the default from examples/single_client_chat/android/native/CMakeLists.txt
  # so repeated runs reuse the downloaded dependencies.
  if (-not $env:CPM_SOURCE_CACHE) {
    $env:CPM_SOURCE_CACHE = Join-Path $repo_root ".cpm_cache"
  }
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
Write-Host "APK                  : $ApkPath"

$devices = Get-AdbDevices $adb
if ($Serial) {
  if ($devices -notcontains $Serial) {
    throw "Requested device '$Serial' is not connected"
  }
} else {
  $emulator = Get-Tool $sdk "emulator\emulator.exe"
  function Get-AvdConfigPath([string]$Avd) {
    $direct = Join-Path $env:USERPROFILE ".android\avd\$Avd.avd\config.ini"
    if (Test-Path $direct) { return $direct }
    throw "AVD config.ini not found for '$Avd'"
  }
  function Get-AvdAbi([string]$ConfigPath) {
    $abi_line = Get-Content $ConfigPath | Where-Object { $_ -match '^\s*abi\.type\s*=' } | Select-Object -First 1
    if ($abi_line -match '^\s*abi\.type\s*=\s*(.+)$') { return $Matches[1].Trim() }
    throw "Unable to parse abi.type from $ConfigPath"
  }

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
    $cfg = Get-AvdConfigPath $preferred
    $abi = Get-AvdAbi $cfg
    if ($abi -ne "x86_64") {
      throw "Preferred AVD $preferred has abi.type=$abi"
    }
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

$device_abi = (& $adb -s $Serial shell getprop ro.product.cpu.abi).Trim()
$device_api = (& $adb -s $Serial shell getprop ro.build.version.sdk).Trim()
Write-Host "Device serial        : $Serial"
Write-Host "Device ABI           : $device_abi"
Write-Host "Device API           : $device_api"

if (-not $SkipInstall) {
  Write-Host "Installing the APK"
  Invoke-Adb $adb $Serial @("install", "-r", "-t", "-g", $ApkPath) | Out-Null
}

Write-Host ""
Write-Host "Phase 1: clean start"
Stop-App $adb $Serial
Invoke-Adb $adb $Serial @("shell", "pm", "clear", $PackageName) | Out-Null
Clear-Logcat $adb $Serial
Start-App $adb $Serial

Wait-Marker $adb $Serial "APPTRAVERSE_NATIVE_RUNTIME_CREATED" "APPTRAVERSE_NATIVE_RUNTIME_CREATED"
Wait-Marker $adb $Serial "AETHER_RUNTIME_READY" "AETHER_RUNTIME_READY"
$domain_line = Wait-Marker $adb $Serial "SINGLE_DOMAIN_READY" "SINGLE_DOMAIN_READY"
Write-Host "  $domain_line"
if ($domain_line -notmatch "match=1") {
  throw "Aether and the application graph are not in the same Domain"
}
if ($domain_line -notmatch "aether_root_id=1") {
  throw "Aether root ObjId is not 1"
}
if ($domain_line -notmatch "app_id=100000") {
  throw "Application ObjId is not 100000"
}
Wait-Marker $adb $Serial "ANDROID_GRAPH_CREATED" "ANDROID_GRAPH_CREATED"
Wait-Marker $adb $Serial "ANDROID_PRESENTERS_LOADED" "ANDROID_PRESENTERS_LOADED"
Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED" "TRANSCRIPT_PUBLISHED"
$phase1 = Save-Phase $adb $Serial "phase 1: clean start"
if ($phase1 -match "ANDROID_GRAPH_LOADED") {
  throw "A clean start must create the graph, not load it"
}

Write-Host ""
Write-Host "Phase 2: send $FirstMessage"
Send-Message $adb $Serial $FirstMessage
Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=$FirstMessage" "MESSAGE_COMMITTED $FirstMessage"
Wait-Marker $adb $Serial "STATE_SAVED" "STATE_SAVED"
Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED .*$FirstMessage" "transcript with $FirstMessage"
Save-Phase $adb $Serial "phase 2: first message" | Out-Null

Write-Host ""
Write-Host "Phase 3: force-stop and relaunch"
Stop-App $adb $Serial
Start-Sleep -Seconds 2
Clear-Logcat $adb $Serial
Start-App $adb $Serial

Wait-Marker $adb $Serial "AETHER_RUNTIME_READY" "AETHER_RUNTIME_READY after relaunch"
$reload_domain_line = Wait-Marker $adb $Serial "SINGLE_DOMAIN_READY" "SINGLE_DOMAIN_READY after relaunch"
if ($reload_domain_line -notmatch "match=1") {
  throw "Aether and the application graph are not in the same Domain after relaunch"
}
Wait-Marker $adb $Serial "ANDROID_GRAPH_LOADED" "ANDROID_GRAPH_LOADED"
Wait-Marker $adb $Serial "ANDROID_PRESENTERS_LOADED" "ANDROID_PRESENTERS_LOADED after relaunch"
Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED .*$FirstMessage" "restored transcript with $FirstMessage"
$phase3 = Save-Phase $adb $Serial "phase 3: relaunch"
if ($phase3 -match "ANDROID_GRAPH_CREATED") {
  throw "A relaunch must load the stored graph, not create a new one"
}

Write-Host ""
Write-Host "Phase 4: rotate and send $SecondMessage"
Clear-Logcat $adb $Serial
Set-Rotation $adb $Serial 1
Start-Sleep -Seconds 2
Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED .*$FirstMessage" "transcript republished after rotation"
Start-Sleep -Seconds 1
Send-Message $adb $Serial $SecondMessage
Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=$SecondMessage" "MESSAGE_COMMITTED $SecondMessage" 120
$transcript_line = Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED .*$SecondMessage" "transcript with $SecondMessage" 120
Write-Host "  $transcript_line"
if ($transcript_line -notmatch $FirstMessage) {
  throw "The transcript lost $FirstMessage after rotation"
}
Save-Phase $adb $Serial "phase 4: rotation" | Out-Null
Set-Rotation $adb $Serial 0

Write-Host ""
Write-Host "Phase 5: relaunch after rotation"
Stop-App $adb $Serial
Start-Sleep -Seconds 2
Clear-Logcat $adb $Serial
Start-App $adb $Serial
Wait-Marker $adb $Serial "ANDROID_GRAPH_LOADED" "ANDROID_GRAPH_LOADED after rotation"
$final_transcript = Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED .*$SecondMessage" "final transcript"
Write-Host "  $final_transcript"
if ($final_transcript -notmatch $FirstMessage) {
  throw "The stored transcript lost $FirstMessage"
}
Save-Phase $adb $Serial "phase 5: final relaunch" | Out-Null

Write-Host ""
Write-Host "Checking for crashes and JNI errors"
$all_logs = ($script:CollectedLogs -join "`n")
Assert-NoMarker $all_logs "FATAL EXCEPTION" "FATAL EXCEPTION"
Assert-NoMarker $all_logs "JNI DETECTED ERROR" "JNI DETECTED ERROR"
Assert-NoMarker $all_logs "JNI ERROR" "JNI ERROR"
Assert-NoMarker $all_logs "Fatal signal" "fatal signal"

Write-Host ""
Write-Host "Android single-client chat persistence smoke passed."
Write-Host "Device serial        : $Serial"
Write-Host "Device ABI           : $device_abi"
Write-Host "Device API           : $device_api"
exit 0
