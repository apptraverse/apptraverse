# Emulator smoke for the Android surfaces demo.
#
# Scenario:
#   1. pm clear, launch, one page: Surface 1
#   2. Add twice -> Surface 2, Surface 3, pager shows 3 pages
#   3. swipe back to page 1, Remove current -> 2 pages left, neighbour shown
#   4. force-stop / relaunch -> the remaining topology is restored from disk
#   5. Remove current on the last page -> whole application stops, Surface kept
#   6. relaunch -> the last Surface is still there

[CmdletBinding()]
param(
  [string]$Serial = "",
  [string]$ApkPath = "",
  [switch]$SkipBuild,
  [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"

$PackageName = "com.apptraverse.surfaces"
$ActivityName = "$PackageName/.MainActivity"
$Abi = "x86_64"
$PreferredAvd = "Aether_NDK_Smoke_x86_64"

function Resolve-RepoRoot {
  $script_dir = Split-Path -Parent $PSCommandPath
  return (Resolve-Path (Join-Path $script_dir "..\..")).Path
}

function Resolve-AndroidSdk {
  foreach ($candidate in @($env:ANDROID_SDK_ROOT, $env:ANDROID_HOME,
                           (Join-Path $env:LOCALAPPDATA "Android\Sdk"))) {
    if ($candidate -and (Test-Path $candidate)) {
      return (Resolve-Path $candidate).Path
    }
  }
  throw "Android SDK not found. Set ANDROID_SDK_ROOT."
}

function Get-Tool([string]$Sdk, [string]$Relative) {
  $path = Join-Path $Sdk $Relative
  if (-not (Test-Path $path)) {
    throw "Required tool not found: $path"
  }
  return $path
}

function Get-AdbDevices([string]$Adb) {
  $devices = @()
  foreach ($line in (& $Adb devices | Select-Object -Skip 1)) {
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
  & $Adb -s $DeviceSerial logcat -c 2>&1 | Out-Null
}

function Get-Logcat([string]$Adb, [string]$DeviceSerial) {
  return (& $Adb -s $DeviceSerial logcat -d -v brief 2>&1 | ForEach-Object { "$_" } | Out-String)
}

function Wait-Marker([string]$Adb, [string]$DeviceSerial, [string]$Pattern,
                     [string]$Description, [int]$TimeoutSec = 90) {
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $logs = Get-Logcat $Adb $DeviceSerial
    $match = ($logs -split "`n" | Where-Object { $_ -match $Pattern } | Select-Object -Last 1)
    if ($match) {
      Write-Host "  OK  $Description"
      return $match.Trim()
    }
    Start-Sleep -Milliseconds 500
  }
  Write-Host "----- logcat -----"
  Write-Host (Get-Logcat $Adb $DeviceSerial)
  throw "Timed out waiting for $Description (pattern: $Pattern)"
}

function Get-UiDump([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "uiautomator", "dump", "/sdcard/surfaces_ui.xml") | Out-Null
  return Invoke-Adb $Adb $DeviceSerial @("shell", "cat", "/sdcard/surfaces_ui.xml")
}

function Get-UiNode([string]$Xml, [string]$ResourceId) {
  $escaped = [regex]::Escape($ResourceId)
  $match = [regex]::Match($Xml, '<node\b[^>]*resource-id="' + $escaped + '"[^>]*/?>')
  if (-not $match.Success) {
    return $null
  }
  $node = $match.Value
  $text = ""
  if ($node -match '\btext="([^"]*)"') { $text = $Matches[1] }
  $x = 0
  $y = 0
  if ($node -match 'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"') {
    $x = [int](([int]$Matches[1] + [int]$Matches[3]) / 2)
    $y = [int](([int]$Matches[2] + [int]$Matches[4]) / 2)
  }
  return [pscustomobject]@{ Text = $text; X = $x; Y = $y }
}

function Assert-Page([string]$Adb, [string]$DeviceSerial, [string]$Title,
                     [string]$Indicator) {
  Start-Sleep -Milliseconds 700
  $xml = Get-UiDump $Adb $DeviceSerial
  $titleNode = Get-UiNode $xml "$PackageName`:id/surface_title"
  $indicatorNode = Get-UiNode $xml "$PackageName`:id/page_indicator"
  if ($null -eq $titleNode -or $titleNode.Text -ne $Title) {
    $got = if ($null -eq $titleNode) { "<missing>" } else { $titleNode.Text }
    throw "Expected page title '$Title', got '$got'"
  }
  if ($null -eq $indicatorNode -or $indicatorNode.Text -ne $Indicator) {
    $got = if ($null -eq $indicatorNode) { "<missing>" } else { $indicatorNode.Text }
    throw "Expected page indicator '$Indicator', got '$got'"
  }
  Write-Host "  OK  page '$Title' ($Indicator)"
}

function Tap-Node([string]$Adb, [string]$DeviceSerial, [string]$ResourceId) {
  $xml = Get-UiDump $Adb $DeviceSerial
  $node = Get-UiNode $xml "$PackageName`:id/$ResourceId"
  if ($null -eq $node -or $node.X -le 0) {
    throw "Unable to locate $ResourceId"
  }
  Invoke-Adb $Adb $DeviceSerial @("shell", "input", "tap", "$($node.X)", "$($node.Y)") | Out-Null
  Start-Sleep -Milliseconds 800
}

function Swipe-Page([string]$Adb, [string]$DeviceSerial, [string]$Direction) {
  # Left swipe moves to the next page, right swipe to the previous one.
  if ($Direction -eq "next") {
    Invoke-Adb $Adb $DeviceSerial @("shell", "input", "swipe", "800", "1000", "200", "1000", "200") | Out-Null
  } else {
    Invoke-Adb $Adb $DeviceSerial @("shell", "input", "swipe", "200", "1000", "800", "1000", "200") | Out-Null
  }
  Start-Sleep -Milliseconds 600
}

function Get-AppPid($Adb, [string]$DeviceSerial, [string]$Package) {
  # adb prints nothing at all once the process is gone.
  $out = & $Adb -s $DeviceSerial shell pidof $Package
  if (-not $out) { return "" }
  return ([string]$out).Trim()
}

function Wait-ProcessExit($Adb, [string]$DeviceSerial, [string]$Package, [string]$After) {
  $deadline = (Get-Date).AddSeconds(30)
  while ((Get-Date) -lt $deadline) {
    if (-not (Get-AppPid $Adb $DeviceSerial $Package)) {
      Write-Host "  OK  application process exited"
      return
    }
    Start-Sleep -Milliseconds 500
  }
  throw "Application process is still running after $After"
}

function Assert-NoFatal([string]$Logs) {
  foreach ($pattern in @("FATAL EXCEPTION", "JNI DETECTED ERROR", "Fatal signal", "SIGSEGV", "Assertion failed")) {
    if ($Logs -match $pattern) {
      throw "Unexpected '$pattern' in logcat"
    }
  }
  Write-Host "  OK  no fatal / assert / JNI errors"
}

$repo_root = Resolve-RepoRoot
$sdk = Resolve-AndroidSdk
$adb = Get-Tool $sdk "platform-tools\adb.exe"
$android_dir = Join-Path $repo_root "examples\surfaces_demo\android"

Write-Host "Repository root      : $repo_root"
Write-Host "Selected Android SDK : $sdk"
Write-Host "Package              : $PackageName"
Write-Host "ABI                  : $Abi"

if (-not $ApkPath) {
  $ApkPath = Join-Path $android_dir "app\build\outputs\apk\debug\app-debug.apk"
}

if (-not $SkipBuild) {
  if (-not $env:JAVA_HOME -or -not (Test-Path $env:JAVA_HOME)) {
    $jdk20 = "C:\Program Files\Java\jdk-20"
    if (-not (Test-Path $jdk20)) {
      throw "No JDK found. Set JAVA_HOME to a JDK the Gradle wrapper accepts."
    }
    $env:JAVA_HOME = $jdk20
  }
  $env:ANDROID_SDK_ROOT = $sdk
  $env:ANDROID_HOME = $sdk
  if (-not $env:CPM_SOURCE_CACHE) { $env:CPM_SOURCE_CACHE = "C:\cpm-cache" }
  Push-Location $android_dir
  try {
    & cmd /c "`"$(Join-Path $android_dir 'gradlew.bat')`" -Papptraverse.abiFilters=$Abi :app:assembleDebug"
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
if (-not $Serial) {
  foreach ($d in $devices) {
    if ((& $adb -s $d shell getprop ro.product.cpu.abi).Trim() -eq $Abi) {
      $Serial = $d
      break
    }
  }
}
if (-not $Serial) {
  $emulator = Get-Tool $sdk "emulator\emulator.exe"
  Write-Host "Starting emulator $PreferredAvd"
  Start-Process -FilePath $emulator -ArgumentList @("-avd", $PreferredAvd, "-no-snapshot-save", "-no-boot-anim") | Out-Null
  $deadline = (Get-Date).AddSeconds(300)
  while ((Get-Date) -lt $deadline -and -not $Serial) {
    Start-Sleep -Seconds 3
    foreach ($d in (Get-AdbDevices $adb)) {
      if ((& $adb -s $d shell getprop ro.product.cpu.abi).Trim() -eq $Abi) {
        $Serial = $d
        break
      }
    }
  }
  if (-not $Serial) { throw "Failed to start an $Abi emulator" }
}

& $adb -s $Serial wait-for-device | Out-Null
$boot_deadline = (Get-Date).AddSeconds(300)
while ((Get-Date) -lt $boot_deadline) {
  if ((& $adb -s $Serial shell getprop sys.boot_completed).Trim() -eq "1") { break }
  Start-Sleep -Seconds 2
}

$device_api = (& $adb -s $Serial shell getprop ro.build.version.sdk).Trim()
Write-Host "Device serial        : $Serial"
Write-Host "Device API           : $device_api"

if (-not $SkipInstall) {
  Write-Host "Installing the APK"
  Invoke-Adb $adb $Serial @("install", "-r", "-t", "-g", $ApkPath) | Out-Null
}

Write-Host ""
Write-Host "Phase 1: clean start"
Invoke-Adb $adb $Serial @("shell", "am", "force-stop", $PackageName) | Out-Null
Invoke-Adb $adb $Serial @("shell", "pm", "clear", $PackageName) | Out-Null
Clear-Logcat $adb $Serial
Invoke-Adb $adb $Serial @("shell", "am", "start", "-W", "-n", $ActivityName) | Out-Null
Wait-Marker $adb $Serial "SURFACES_NATIVE_RUNTIME_CREATED" "native runtime created"
Wait-Marker $adb $Serial "SURFACES_UI_READY" "GUI mirror + presenters ready"
Wait-Marker $adb $Serial "SURFACE_PAGE_LOADED number=1" "Surface 1 page presenter"
Assert-Page $adb $Serial "Surface 1" "1 / 1"

Write-Host ""
Write-Host "Phase 2: Add twice"
Tap-Node $adb $Serial "add_surface"
Wait-Marker $adb $Serial "SURFACE_PAGE_LOADED number=2" "Surface 2 page presenter"
Tap-Node $adb $Serial "add_surface"
Wait-Marker $adb $Serial "SURFACE_PAGE_LOADED number=3" "Surface 3 page presenter"
Assert-Page $adb $Serial "Surface 1" "1 / 3"

Write-Host ""
Write-Host "Phase 3: swipe through the pager"
Swipe-Page $adb $Serial "next"
Assert-Page $adb $Serial "Surface 2" "2 / 3"
Swipe-Page $adb $Serial "next"
Assert-Page $adb $Serial "Surface 3" "3 / 3"
Swipe-Page $adb $Serial "prev"
Assert-Page $adb $Serial "Surface 2" "2 / 3"

Write-Host ""
Write-Host "Phase 4: Remove current (Surface 2)"
Tap-Node $adb $Serial "remove_current"
Wait-Marker $adb $Serial "SURFACE_PAGE_UNLOADED number=2" "Surface 2 page unloaded"
Assert-Page $adb $Serial "Surface 3" "2 / 2"

Write-Host ""
Write-Host "Phase 5b: model-driven controls orientation (portrait / landscape)"
# Drive rotation via user_rotation while auto-rotate is off. Always restore
# accelerometer_rotation=1 afterwards so the emulator rotate buttons keep working.
try {
  Invoke-Adb $adb $Serial @("shell", "settings", "put", "system", "accelerometer_rotation", "0") | Out-Null

  Clear-Logcat $adb $Serial
  Invoke-Adb $adb $Serial @("shell", "settings", "put", "system", "user_rotation", "1") | Out-Null
  Wait-Marker $adb $Serial "CONTROLS_ORIENTATION wide=1" "landscape -> horizontal controls"
  Wait-Marker $adb $Serial "SURFACES_PRESENTATION_SIZE" "landscape presentation size reported"

  Clear-Logcat $adb $Serial
  Invoke-Adb $adb $Serial @("shell", "settings", "put", "system", "user_rotation", "0") | Out-Null
  Wait-Marker $adb $Serial "CONTROLS_ORIENTATION wide=0" "portrait -> vertical controls"

  Clear-Logcat $adb $Serial
  Invoke-Adb $adb $Serial @("shell", "settings", "put", "system", "user_rotation", "1") | Out-Null
  Wait-Marker $adb $Serial "CONTROLS_ORIENTATION wide=1" "landscape again -> horizontal controls"
} finally {
  Invoke-Adb $adb $Serial @("shell", "settings", "put", "system", "user_rotation", "0") | Out-Null
  Invoke-Adb $adb $Serial @("shell", "settings", "put", "system", "accelerometer_rotation", "1") | Out-Null
  Start-Sleep -Seconds 1
}

Write-Host ""
Write-Host "Phase 5: Back saves, relaunch restores topology and current page"
# Back is the controlled shutdown path and the only save point: a force-stop or
# a system kill leaves the last changes unsaved.
Invoke-Adb $adb $Serial @("shell", "input", "keyevent", "4") | Out-Null
Wait-Marker $adb $Serial "SURFACES_STATE_SAVED" "state saved on Back"
Wait-Marker $adb $Serial "SURFACES_APP_STOPPED" "native teardown finished"
Wait-ProcessExit $adb $Serial $PackageName "Back"
Clear-Logcat $adb $Serial
Invoke-Adb $adb $Serial @("shell", "am", "start", "-W", "-n", $ActivityName) | Out-Null
Wait-Marker $adb $Serial "SURFACES_PAGES numbers=1,3, count=2" "restored pages 1 and 3"
# Surface 3 was current when the application stopped.
Assert-Page $adb $Serial "Surface 3" "2 / 2"

Write-Host ""
Write-Host "Phase 6: Remove current down to the last page stops the application"
Tap-Node $adb $Serial "remove_current"
Wait-Marker $adb $Serial "SURFACE_PAGE_UNLOADED number=3" "Surface 3 page unloaded"
Assert-Page $adb $Serial "Surface 1" "1 / 1"
Clear-Logcat $adb $Serial
Tap-Node $adb $Serial "remove_current"
Wait-Marker $adb $Serial "SURFACES_LAST_PAGE_STOP" "last page requests application stop"
Wait-Marker $adb $Serial "SURFACES_STATE_SAVED" "state saved on shutdown"
Wait-Marker $adb $Serial "SURFACES_UI_UNLOADED" "presenters unloaded"
Wait-ProcessExit $adb $Serial $PackageName "the last Remove current"

Write-Host ""
Write-Host "Phase 7: the last Surface survived the shutdown"
Clear-Logcat $adb $Serial
Invoke-Adb $adb $Serial @("shell", "am", "start", "-W", "-n", $ActivityName) | Out-Null
Wait-Marker $adb $Serial "SURFACES_PAGES numbers=1, count=1" "Surface 1 restored"
Assert-Page $adb $Serial "Surface 1" "1 / 1"

Assert-NoFatal (Get-Logcat $adb $Serial)

Write-Host ""
Write-Host "Android surfaces pager smoke passed."
Write-Host "Device serial        : $Serial"
Write-Host "Device API           : $device_api"
Write-Host "APK                  : $ApkPath"
exit 0
