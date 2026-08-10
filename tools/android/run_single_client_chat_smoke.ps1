# Persistence and rotation smoke for the Android single-client chat example.
#
# Scenario:
#   1. pm clear, launch portrait, create graph
#   2. send before_rotation
#   3. landscape: Activity recreation, transcript kept, NativeRuntime reused
#   4. send in_landscape
#   5. portrait again: Activity recreation, both messages kept
#   6. send after_rotation
#   7. force-stop / relaunch: all three messages restored, EditText empty
#   8. restore original emulator rotation settings in finally

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
$MsgBefore = "before_rotation"
$MsgLandscape = "in_landscape"
$MsgAfter = "after_rotation"

$script:CollectedLogs = New-Object System.Collections.Generic.List[string]
$script:SavedAccelerometerRotation = $null
$script:SavedUserRotation = $null
$script:RotationSettingsTouched = $false

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

function Count-Marker([string]$Logs, [string]$Pattern) {
  return ([regex]::Matches($Logs, $Pattern)).Count
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

function Get-AppPid([string]$Adb, [string]$DeviceSerial) {
  $raw = (Invoke-Adb $Adb $DeviceSerial @("shell", "pidof", $PackageName)).Trim()
  if (-not $raw) {
    throw "pidof returned empty for $PackageName"
  }
  return ($raw -split '\s+')[0]
}

function Get-UiRootSize([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "uiautomator", "dump", "/sdcard/apptraverse_ui.xml") | Out-Null
  $xml = Invoke-Adb $Adb $DeviceSerial @("shell", "cat", "/sdcard/apptraverse_ui.xml")
  if ($xml -match 'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"') {
    $left = [int]$Matches[1]
    $top = [int]$Matches[2]
    $right = [int]$Matches[3]
    $bottom = [int]$Matches[4]
    return [pscustomobject]@{
      Width = $right - $left
      Height = $bottom - $top
      Xml = $xml
    }
  }
  throw "Unable to parse root bounds from UI dump"
}

function Assert-OrientationSize([string]$Expected, [int]$Width, [int]$Height) {
  if ($Expected -eq "portrait") {
    if (-not ($Height -gt $Width)) {
      throw "Expected portrait root_height > root_width, got ${Width}x${Height}"
    }
  } elseif ($Expected -eq "landscape") {
    if (-not ($Width -gt $Height)) {
      throw "Expected landscape root_width > root_height, got ${Width}x${Height}"
    }
  } else {
    throw "Unknown orientation expectation: $Expected"
  }
  Write-Host "  OK  $Expected size ${Width}x${Height}"
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
  return [pscustomobject]@{
    Node = $node
    Text = $text
    X = $x
    Y = $y
  }
}

function Assert-TranscriptContains([string]$Xml, [string]$Text) {
  # uiautomator may put newlines as &#10; or keep plain text in the attribute.
  $normalized = $Xml -replace '&#10;', ' ' -replace '\s+', ' '
  if ($normalized -notmatch [regex]::Escape($Text)) {
    throw "Transcript UI does not contain '$Text'"
  }
  Write-Host "  OK  transcript contains $Text"
}

function Assert-EditTextEmpty([string]$Xml) {
  $node = Get-UiNode $Xml "$PackageName`:id/message_input"
  if ($null -eq $node) {
    throw "EditText node not found while asserting empty"
  }
  # Older uiautomator dumps may report the hint as text when the field is empty.
  if ($node.Text -ne "" -and $node.Text -ne "Message") {
    throw "EditText expected empty, got '$($node.Text)'"
  }
  Write-Host "  OK  EditText empty"
}

function Send-Message([string]$Adb, [string]$DeviceSerial, [string]$Text) {
  Start-Sleep -Seconds 1
  Invoke-Adb $Adb $DeviceSerial @("shell", "uiautomator", "dump", "/sdcard/apptraverse_ui.xml") | Out-Null
  $xml = Invoke-Adb $Adb $DeviceSerial @("shell", "cat", "/sdcard/apptraverse_ui.xml")
  $input = Get-UiNode $xml "$PackageName`:id/message_input"
  if ($null -eq $input -or $input.X -le 0) {
    throw "Unable to locate message_input"
  }
  Invoke-Adb $Adb $DeviceSerial @("shell", "input", "tap", "$($input.X)", "$($input.Y)") | Out-Null
  Start-Sleep -Milliseconds 400
  # Clear any leftover text in one adb round-trip (input text always appends).
  $clear_args = @("shell", "input", "keyevent", "123")
  for ($i = 0; $i -lt 64; $i++) {
    $clear_args += "67"
  }
  Invoke-Adb $Adb $DeviceSerial $clear_args | Out-Null
  Start-Sleep -Milliseconds 300
  Invoke-Adb $Adb $DeviceSerial @("shell", "input", "text", $Text) | Out-Null
  Start-Sleep -Milliseconds 500
  Invoke-Adb $Adb $DeviceSerial @("shell", "uiautomator", "dump", "/sdcard/apptraverse_ui.xml") | Out-Null
  $xml = Invoke-Adb $Adb $DeviceSerial @("shell", "cat", "/sdcard/apptraverse_ui.xml")
  $typed = Get-UiNode $xml "$PackageName`:id/message_input"
  if ($null -eq $typed -or $typed.Text -ne $Text) {
    $got = if ($null -eq $typed) { "<missing>" } else { $typed.Text }
    throw "EditText does not contain expected text '$Text' before Send (got '$got')"
  }
  # Prefer IME action/Enter so landscape soft-keyboard cannot cover the Send button.
  Invoke-Adb $Adb $DeviceSerial @("shell", "input", "keyevent", "66") | Out-Null
}

function Get-Setting([string]$Adb, [string]$DeviceSerial, [string]$Name) {
  return (Invoke-Adb $Adb $DeviceSerial @("shell", "settings", "get", "system", $Name)).Trim()
}

function Set-Setting([string]$Adb, [string]$DeviceSerial, [string]$Name, [string]$Value) {
  if ($null -eq $Value -or $Value -eq "" -or $Value -eq "null") {
    return
  }
  Invoke-Adb $Adb $DeviceSerial @("shell", "settings", "put", "system", $Name, $Value) | Out-Null
}

function Set-ForcedRotation([string]$Adb, [string]$DeviceSerial, [int]$Rotation) {
  $script:RotationSettingsTouched = $true
  Invoke-Adb $Adb $DeviceSerial @(
    "shell", "settings", "put", "system", "accelerometer_rotation", "0") | Out-Null
  Invoke-Adb $Adb $DeviceSerial @(
    "shell", "settings", "put", "system", "user_rotation", "$Rotation") | Out-Null
  Start-Sleep -Seconds 3
}

function Restore-RotationSettings([string]$Adb, [string]$DeviceSerial) {
  if (-not $script:RotationSettingsTouched) {
    return
  }
  Write-Host "Restoring emulator rotation settings"
  Write-Host "  accelerometer_rotation -> $($script:SavedAccelerometerRotation)"
  Write-Host "  user_rotation          -> $($script:SavedUserRotation)"
  Set-Setting $Adb $DeviceSerial "accelerometer_rotation" $script:SavedAccelerometerRotation
  Set-Setting $Adb $DeviceSerial "user_rotation" $script:SavedUserRotation
}

function Get-LatestActivityInstance([string]$Logs) {
  $matches = [regex]::Matches($Logs, "ACTIVITY_CREATED instance=([0-9a-fA-F]+)")
  if ($matches.Count -eq 0) {
    throw "No ACTIVITY_CREATED marker found"
  }
  return $matches[$matches.Count - 1].Groups[1].Value
}

function Get-LatestJournalSize([string]$Logs, [string]$Kind) {
  $pattern = if ($Kind -eq "window") {
    "WINDOW_JOURNAL_SIZE n=(\d+)"
  } else {
    "CHAT_JOURNAL_SIZE n=(\d+)"
  }
  $matches = [regex]::Matches($Logs, $pattern)
  if ($matches.Count -eq 0) {
    throw "No $Kind journal size marker found"
  }
  return [int]$matches[$matches.Count - 1].Groups[1].Value
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
  if (-not $env:JAVA_HOME -or -not (Test-Path $env:JAVA_HOME)) {
    $jdk20 = "C:\Program Files\Java\jdk-20"
    $jbr = Join-Path ${env:ProgramFiles} "Android\Android Studio\jbr"
    if (Test-Path $jdk20) {
      $env:JAVA_HOME = $jdk20
    } elseif (Test-Path $jbr) {
      $env:JAVA_HOME = $jbr
    } else {
      throw "No JDK found. Set JAVA_HOME to JDK 17-23 (Gradle 8.13 rejects JBR 25)."
    }
  }
  Write-Host "JAVA_HOME            : $env:JAVA_HOME"
  $env:ANDROID_SDK_ROOT = $sdk
  $env:ANDROID_HOME = $sdk
  if (-not $env:CPM_SOURCE_CACHE) {
    $env:CPM_SOURCE_CACHE = "C:\cpm-cache"
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
    $cfg_abi = Get-AvdAbi $cfg
    if ($cfg_abi -ne "x86_64") {
      throw "Preferred AVD $preferred has abi.type=$cfg_abi"
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

$script:SavedAccelerometerRotation = Get-Setting $adb $Serial "accelerometer_rotation"
$script:SavedUserRotation = Get-Setting $adb $Serial "user_rotation"
Write-Host "Saved accelerometer_rotation : $($script:SavedAccelerometerRotation)"
Write-Host "Saved user_rotation          : $($script:SavedUserRotation)"

try {
  Write-Host ""
  Write-Host "Phase 1: clean start portrait"
  Stop-App $adb $Serial
  Invoke-Adb $adb $Serial @("shell", "pm", "clear", $PackageName) | Out-Null
  Set-ForcedRotation $adb $Serial 0
  Clear-Logcat $adb $Serial
  Start-App $adb $Serial

  Wait-Marker $adb $Serial "APPTRAVERSE_NATIVE_RUNTIME_CREATED" "APPTRAVERSE_NATIVE_RUNTIME_CREATED"
  Wait-Marker $adb $Serial "AETHER_RUNTIME_READY" "AETHER_RUNTIME_READY"
  $domain_line = Wait-Marker $adb $Serial "SINGLE_DOMAIN_READY" "SINGLE_DOMAIN_READY"
  Write-Host "  $domain_line"
  if ($domain_line -notmatch "match=1") {
    throw "Aether and the application graph are not in the same Domain"
  }
  Wait-Marker $adb $Serial "ANDROID_GRAPH_CREATED" "ANDROID_GRAPH_CREATED"
  Wait-Marker $adb $Serial "ANDROID_PRESENTERS_LOADED" "ANDROID_PRESENTERS_LOADED"
  Wait-Marker $adb $Serial "ACTIVITY_CREATED" "ACTIVITY_CREATED"
  Wait-Marker $adb $Serial "ACTIVITY_VIEWPORT" "ACTIVITY_VIEWPORT"
  Wait-Marker $adb $Serial "WINDOW_CHANGED" "WINDOW_CHANGED first viewport"
  Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED" "TRANSCRIPT_PUBLISHED"
  $phase1 = Save-Phase $adb $Serial "phase 1: clean start"
  if ($phase1 -match "ANDROID_GRAPH_LOADED") {
    throw "A clean start must create the graph, not load it"
  }
  $pid_before = Get-AppPid $adb $Serial
  $activity_before = Get-LatestActivityInstance $phase1
  $chat_before_send = Get-LatestJournalSize $phase1 "chat"
  $window_after_first_viewport = Get-LatestJournalSize $phase1 "window"
  if ($chat_before_send -ne 1) {
    throw "Clean graph Chat journal expected 1 (Join), got $chat_before_send"
  }
  if ($window_after_first_viewport -lt 1) {
    throw "First viewport must create at least one Window journal event"
  }
  Write-Host "  PID=$pid_before activity=$activity_before chat=$chat_before_send window=$window_after_first_viewport"
  $ui = Get-UiRootSize $adb $Serial
  Assert-OrientationSize "portrait" $ui.Width $ui.Height
  Assert-EditTextEmpty $ui.Xml

  Write-Host ""
  Write-Host "Phase 2: send $MsgBefore"
  Send-Message $adb $Serial $MsgBefore
  Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=$MsgBefore" "MESSAGE_COMMITTED $MsgBefore"
  Wait-Marker $adb $Serial "STATE_SAVED" "STATE_SAVED"
  Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED .*$MsgBefore" "transcript with $MsgBefore"
  $phase2 = Save-Phase $adb $Serial "phase 2: before_rotation"
  $chat_after_first = Get-LatestJournalSize $phase2 "chat"
  if ($chat_after_first -ne ($chat_before_send + 1)) {
    throw "Chat journal after first send expected $($chat_before_send + 1), got $chat_after_first"
  }
  $window_after_first_send = Get-LatestJournalSize $phase2 "window"

  Write-Host ""
  Write-Host "Phase 3: landscape recreation"
  Clear-Logcat $adb $Serial
  $created_before_rotation = Count-Marker (Get-Logcat $adb $Serial) "APPTRAVERSE_NATIVE_RUNTIME_CREATED"
  $ready_before_rotation = Count-Marker (Get-Logcat $adb $Serial) "AETHER_RUNTIME_READY"
  Set-ForcedRotation $adb $Serial 1
  Wait-Marker $adb $Serial "ACTIVITY_DESTROYED instance=$activity_before" "ACTIVITY_DESTROYED old instance"
  Wait-Marker $adb $Serial "ACTIVITY_CREATED instance=" "ACTIVITY_CREATED landscape instance"
  Wait-Marker $adb $Serial "ACTIVITY_VIEWPORT" "ACTIVITY_VIEWPORT landscape"
  Wait-Marker $adb $Serial "WINDOW_CHANGED" "WINDOW_CHANGED landscape"
  Start-Sleep -Seconds 1
  $ui = Get-UiRootSize $adb $Serial
  Assert-TranscriptContains $ui.Xml $MsgBefore
  $phase3 = Save-Phase $adb $Serial "phase 3: landscape"
  $activity_landscape = Get-LatestActivityInstance $phase3
  if ($activity_landscape -eq $activity_before) {
    throw "Activity instance did not change after landscape rotation"
  }
  $pid_landscape = Get-AppPid $adb $Serial
  if ($pid_landscape -ne $pid_before) {
    throw "PID changed during landscape rotation: $pid_before -> $pid_landscape"
  }
  if ((Count-Marker $phase3 "APPTRAVERSE_NATIVE_RUNTIME_CREATED") -ne $created_before_rotation) {
    throw "NativeRuntime was created again during landscape rotation"
  }
  if ((Count-Marker $phase3 "AETHER_RUNTIME_READY") -ne $ready_before_rotation) {
    throw "AetherApp became ready again during landscape rotation"
  }
  $chat_landscape = Get-LatestJournalSize $phase3 "chat"
  $window_landscape = Get-LatestJournalSize $phase3 "window"
  if ($chat_landscape -ne $chat_after_first) {
    throw "Rotation changed Chat journal size: $chat_after_first -> $chat_landscape"
  }
  if ($window_landscape -le $window_after_first_send) {
    throw "Landscape rotation must increase Window journal"
  }
  Assert-OrientationSize "landscape" $ui.Width $ui.Height
  Assert-EditTextEmpty $ui.Xml

  Write-Host ""
  Write-Host "Phase 4: send $MsgLandscape"
  Send-Message $adb $Serial $MsgLandscape
  Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=$MsgLandscape" "MESSAGE_COMMITTED $MsgLandscape"
  Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED .*$MsgLandscape" "transcript with $MsgLandscape"
  $phase4 = Save-Phase $adb $Serial "phase 4: in_landscape"
  $chat_after_second = Get-LatestJournalSize $phase4 "chat"
  if ($chat_after_second -ne ($chat_after_first + 1)) {
    throw "Chat journal after second send expected $($chat_after_first + 1), got $chat_after_second"
  }

  Write-Host ""
  Write-Host "Phase 5: portrait again"
  Clear-Logcat $adb $Serial
  Set-ForcedRotation $adb $Serial 0
  Wait-Marker $adb $Serial "ACTIVITY_DESTROYED instance=$activity_landscape" "ACTIVITY_DESTROYED landscape instance"
  Wait-Marker $adb $Serial "ACTIVITY_CREATED instance=" "ACTIVITY_CREATED portrait instance"
  Wait-Marker $adb $Serial "WINDOW_CHANGED" "WINDOW_CHANGED portrait return"
  Start-Sleep -Seconds 1
  $ui = Get-UiRootSize $adb $Serial
  Assert-TranscriptContains $ui.Xml $MsgBefore
  Assert-TranscriptContains $ui.Xml $MsgLandscape
  $phase5 = Save-Phase $adb $Serial "phase 5: portrait again"
  $activity_portrait2 = Get-LatestActivityInstance $phase5
  if ($activity_portrait2 -eq $activity_landscape -or $activity_portrait2 -eq $activity_before) {
    throw "Portrait Activity instance was not a new recreation"
  }
  $pid_portrait2 = Get-AppPid $adb $Serial
  if ($pid_portrait2 -ne $pid_before) {
    throw "PID changed during portrait return: $pid_before -> $pid_portrait2"
  }
  if ((Count-Marker $phase5 "APPTRAVERSE_NATIVE_RUNTIME_CREATED") -gt 0) {
    throw "NativeRuntime was created again during portrait return"
  }
  if ((Count-Marker $phase5 "AETHER_RUNTIME_READY") -gt 0) {
    throw "AetherApp became ready again during portrait return"
  }
  $chat_portrait2 = Get-LatestJournalSize $phase5 "chat"
  $window_portrait2 = Get-LatestJournalSize $phase5 "window"
  if ($chat_portrait2 -ne $chat_after_second) {
    throw "Portrait return changed Chat journal size"
  }
  if ($window_portrait2 -le $window_landscape) {
    throw "Portrait return must increase Window journal"
  }
  Assert-OrientationSize "portrait" $ui.Width $ui.Height
  Assert-EditTextEmpty $ui.Xml

  Write-Host ""
  Write-Host "Phase 6: send $MsgAfter"
  Send-Message $adb $Serial $MsgAfter
  Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=$MsgAfter" "MESSAGE_COMMITTED $MsgAfter"
  Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED .*$MsgAfter" "transcript with $MsgAfter"
  $phase6 = Save-Phase $adb $Serial "phase 6: after_rotation"
  $chat_after_third = Get-LatestJournalSize $phase6 "chat"
  if ($chat_after_third -ne ($chat_after_second + 1)) {
    throw "Chat journal after third send expected $($chat_after_second + 1), got $chat_after_third"
  }

  Write-Host ""
  Write-Host "Phase 7: force-stop and relaunch"
  Stop-App $adb $Serial
  Start-Sleep -Seconds 2
  Clear-Logcat $adb $Serial
  Start-App $adb $Serial
  Wait-Marker $adb $Serial "ANDROID_GRAPH_LOADED" "ANDROID_GRAPH_LOADED"
  Wait-Marker $adb $Serial "WINDOW_CHANGED" "WINDOW_CHANGED startup after relaunch"
  Start-Sleep -Seconds 1
  $ui = Get-UiRootSize $adb $Serial
  Assert-TranscriptContains $ui.Xml $MsgBefore
  Assert-TranscriptContains $ui.Xml $MsgLandscape
  Assert-TranscriptContains $ui.Xml $MsgAfter
  $phase7 = Save-Phase $adb $Serial "phase 7: relaunch"
  Assert-EditTextEmpty $ui.Xml
  $chat_relaunch = Get-LatestJournalSize $phase7 "chat"
  $window_relaunch = Get-LatestJournalSize $phase7 "window"
  if ($chat_relaunch -ne $chat_after_third) {
    throw "Relaunch changed Chat journal size"
  }
  if ($window_relaunch -le $window_portrait2) {
    throw "Process relaunch must add a startup WindowChangedEvent"
  }

  Write-Host ""
  Write-Host "Checking for crashes and JNI errors"
  $all_logs = ($script:CollectedLogs -join "`n")
  Assert-NoMarker $all_logs "FATAL EXCEPTION" "FATAL EXCEPTION"
  Assert-NoMarker $all_logs "JNI DETECTED ERROR" "JNI DETECTED ERROR"
  Assert-NoMarker $all_logs "JNI ERROR" "JNI ERROR"
  Assert-NoMarker $all_logs "Fatal signal" "fatal signal"
  Assert-NoMarker $all_logs "SIGSEGV" "SIGSEGV"

  Write-Host ""
  Write-Host "Phase 8: smoke cleanup leaves a clean app"
  Stop-App $adb $Serial
  Invoke-Adb $adb $Serial @("shell", "pm", "clear", $PackageName) | Out-Null
  Clear-Logcat $adb $Serial
  Start-App $adb $Serial
  Wait-Marker $adb $Serial "ANDROID_GRAPH_CREATED" "clean graph after pm clear"
  $clean = Wait-Marker $adb $Serial "TRANSCRIPT_PUBLISHED" "clean transcript"
  Write-Host "  $clean"
  if ($clean -match $MsgBefore -or $clean -match $MsgLandscape -or $clean -match $MsgAfter) {
    throw "Cleanup left test messages in the transcript"
  }
  if ($clean -notmatch "Alice joined") {
    throw "Clean transcript must contain Alice joined"
  }
  $phase8 = Save-Phase $adb $Serial "phase 8: cleanup"
  $ui = Get-UiRootSize $adb $Serial
  Assert-EditTextEmpty $ui.Xml
  if ($ui.Xml -match 'resource-id="com\.apptraverse\.singleclientchat:id/status"') {
    throw "status TextView must not exist in the cleaned UI"
  }

  Write-Host ""
  Write-Host "Android single-client chat rotation/persistence smoke passed."
  Write-Host "Device serial        : $Serial"
  Write-Host "Device ABI           : $device_abi"
  Write-Host "Device API           : $device_api"
  Write-Host "PID                  : $pid_before"
  Write-Host "Activity instances   : $activity_before -> $activity_landscape -> $activity_portrait2"
  Write-Host "Chat journal sizes   : $chat_before_send -> $chat_after_first -> $chat_after_second -> $chat_after_third"
  Write-Host "Window journal sizes : $window_after_first_viewport -> $window_landscape -> $window_portrait2 -> $window_relaunch"
}
finally {
  Restore-RotationSettings $adb $Serial
  $restored_acc = Get-Setting $adb $Serial "accelerometer_rotation"
  $restored_user = Get-Setting $adb $Serial "user_rotation"
  Write-Host "Restored accelerometer_rotation : $restored_acc"
  Write-Host "Restored user_rotation          : $restored_user"
}

exit 0
