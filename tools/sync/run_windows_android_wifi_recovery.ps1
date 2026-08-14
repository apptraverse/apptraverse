# Focused Windows <-> Android Wi-Fi recovery smoke (not full lifecycle matrix).
# Builds with DEFAULT remote Aether pin (no local CPM override). First PASS after
# commits is the validation freeze target.

[CmdletBinding()]
param(
  [string]$Serial = "",
  [string]$ExePath = "",
  [string]$ApkPath = "",
  [string]$WindowsBuildDir = "",
  [string]$AndroidDir = "",
  [ValidateSet("x86_64", "arm64-v8a")]
  [string]$Abi = "x86_64",
  [switch]$SkipAndroidBuild,
  [switch]$SkipWindowsBuild,
  [switch]$SkipInstall,
  [int]$ClientReadyTimeoutSec = 180,
  [int]$SyncTimeoutSec = 180,
  [int]$OfflineTimeoutSec = 45,
  [int]$OutageSec = 18,
  [int]$DeliveryTimeoutMs = 1000,
  [string]$ExpectedAetherSha = "7294f92a0cf749c5d56eedc28673d8089d1f5cb2",
  [int]$ExpectedTaskMax = 128,
  [int]$ExpectedQuarantineMs = 500
)

$ErrorActionPreference = "Stop"
$PackageName = "com.apptraverse.singleclientchat"
$ActivityName = "$PackageName/.MainActivity"
$WindowsClientName = "apptraverse-windows-wifi-recovery"
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$script:WinProc = $null
$script:WinStdoutPath = $null
$script:WinStderrPath = $null
$script:NetworkDisabled = $false
$script:WindowsUid = $null
$script:AndroidUid = $null
$script:Result = "FAIL"
$script:Detail = ""

function Resolve-RepoRoot {
  $script_dir = Split-Path -Parent $PSCommandPath
  return (Resolve-Path (Join-Path $script_dir "..\..")).Path
}
function Resolve-AndroidSdk {
  if ($env:ANDROID_SDK_ROOT -and (Test-Path $env:ANDROID_SDK_ROOT)) { return (Resolve-Path $env:ANDROID_SDK_ROOT).Path }
  if ($env:ANDROID_HOME -and (Test-Path $env:ANDROID_HOME)) { return (Resolve-Path $env:ANDROID_HOME).Path }
  $default = Join-Path $env:LOCALAPPDATA "Android\Sdk"
  if (Test-Path $default) { return (Resolve-Path $default).Path }
  throw "Android SDK not found"
}
function Get-Tool([string]$Sdk, [string]$Relative) {
  $path = Join-Path $Sdk $Relative
  if (-not (Test-Path $path)) { throw "Required tool not found: $path" }
  return $path
}
function Write-Utf8NoBom([string]$Path, [string]$Text) {
  [System.IO.File]::WriteAllText($Path, $Text, $Utf8NoBom)
}
function Add-Utf8NoBom([string]$Path, [string]$Text) {
  [System.IO.File]::AppendAllText($Path, $Text, $Utf8NoBom)
}
function Ensure-JavaHome {
  if ($env:JAVA_HOME -and (Test-Path $env:JAVA_HOME)) { return }
  $jdk20 = "C:\Program Files\Java\jdk-20"
  $jbr = Join-Path ${env:ProgramFiles} "Android\Android Studio\jbr"
  if (Test-Path $jdk20) { $env:JAVA_HOME = $jdk20 }
  elseif (Test-Path $jbr) { $env:JAVA_HOME = $jbr }
  else { throw "No JDK found. Set JAVA_HOME to JDK 17-23." }
}
function Get-AdbDevices([string]$Adb) {
  $devices = @()
  foreach ($line in (& $Adb devices | Select-Object -Skip 1)) {
    if ($line -match "^(\S+)\s+device\s*$") { $devices += $Matches[1] }
  }
  return $devices
}
function Invoke-Adb([string]$Adb, [string]$DeviceSerial, [string[]]$Arguments) {
  $prev = $ErrorActionPreference; $ErrorActionPreference = "Continue"
  $output = & $Adb -s $DeviceSerial @Arguments 2>&1 | ForEach-Object { "$_" } | Out-String
  $code = $LASTEXITCODE; $ErrorActionPreference = $prev
  if ($code -ne 0) { throw "adb $($Arguments -join ' ') failed ($code)`n$output" }
  return $output
}
function Clear-Logcat([string]$Adb, [string]$DeviceSerial) {
  & $Adb -s $DeviceSerial logcat -c 2>&1 | Out-Null
}
function Get-Logcat([string]$Adb, [string]$DeviceSerial) {
  return (& $Adb -s $DeviceSerial logcat -d -v brief 2>&1 | ForEach-Object { "$_" } | Out-String)
}
function Start-App([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "am", "start", "-W", "-n", $ActivityName) | Out-Null
}
function Start-AppNoWait([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "am", "start", "-n", $ActivityName) | Out-Null
}
function Stop-App([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "am", "force-stop", $PackageName) | Out-Null
}
function Wait-Marker([string]$Adb, [string]$DeviceSerial, [string]$Pattern, [string]$Description, [int]$TimeoutSec = 90) {
  $deadline = (Get-Date).AddSeconds($TimeoutSec); $logs = ""
  while ((Get-Date) -lt $deadline) {
    $logs = Get-Logcat $Adb $DeviceSerial
    $match = ($logs -split "`n" | Where-Object { $_ -match $Pattern } | Select-Object -Last 1)
    if ($match) { Write-Host "  OK  $Description"; return $match.Trim() }
    Start-Sleep -Milliseconds 350
  }
  throw "Timed out waiting for $Description (pattern: $Pattern)"
}
function Get-UidFromMarker([string]$Line, [string]$Platform) {
  if ($Line -notmatch "AETHER_CLIENT_READY platform=$Platform uid=([0-9a-fA-F-]+)") {
    throw "Unable to parse $Platform UID from: $Line"
  }
  return $Matches[1]
}
function Parse-BuildInfo([string]$Line) {
  if ($Line -notmatch "AETHER_BUILD_INFO platform=(\w+) git=([0-9a-fA-F]+) quarantine_ms=(\d+) task_max=(\d+)") {
    throw "Unable to parse AETHER_BUILD_INFO from: $Line"
  }
  return [pscustomobject]@{
    Platform = $Matches[1]
    Git = $Matches[2]
    QuarantineMs = [int]$Matches[3]
    TaskMax = [int]$Matches[4]
    Line = $Line.Trim()
  }
}
function Assert-BuildInfoMatches($Info, [string]$Label) {
  $ok_git = ($Info.Git -eq $ExpectedAetherSha) -or ($ExpectedAetherSha.StartsWith($Info.Git)) -or ($Info.Git.StartsWith($ExpectedAetherSha.Substring(0, [Math]::Min(8, $ExpectedAetherSha.Length))))
  # Prefer full SHA equality; also accept if AE_GIT_VERSION is full and matches.
  $ok_git = ($Info.Git -eq $ExpectedAetherSha)
  if (-not $ok_git) {
    throw "AETHER_VERSION_MISMATCH $Label git=$($Info.Git) expected=$ExpectedAetherSha"
  }
  if ($Info.TaskMax -ne $ExpectedTaskMax) {
    throw "AETHER_VERSION_MISMATCH $Label task_max=$($Info.TaskMax) expected=$ExpectedTaskMax"
  }
  if ($Info.QuarantineMs -ne $ExpectedQuarantineMs) {
    throw "AETHER_VERSION_MISMATCH $Label quarantine_ms=$($Info.QuarantineMs) expected=$ExpectedQuarantineMs"
  }
}

function Read-LogFileShared([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path)) { return "" }
  $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, ([System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete))
  try {
    $sr = New-Object System.IO.StreamReader($fs, $Utf8NoBom, $true)
    try { return $sr.ReadToEnd() } finally { $sr.Dispose() }
  } finally { $fs.Dispose() }
}

function Get-WindowsLogText {
  $parts = New-Object System.Collections.Generic.List[string]
  foreach ($p in @($script:WinStdoutPath, $script:WinStderrPath)) {
    if ([string]::IsNullOrEmpty($p)) { continue }
    $parts.Add((Read-LogFileShared $p))
  }
  return ($parts -join "`n")
}
function Wait-WindowsMarker([System.Diagnostics.Process]$Process, [string]$Pattern, [string]$Description, [int]$TimeoutSec) {
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $text = Get-WindowsLogText
    $match = ($text -split "`n" | Where-Object { $_ -match $Pattern } | Select-Object -Last 1)
    if ($match) { Write-Host "  OK  $Description"; return $match.Trim() }
    if ($null -ne $Process -and $Process.HasExited) {
      throw "Windows exited (code=$($Process.ExitCode)) waiting for $Description`n$text"
    }
    Start-Sleep -Milliseconds 300
  }
  throw "Timed out waiting for Windows $Description`n$(Get-WindowsLogText)"
}
function Start-WindowsRedirectedProcess([string]$Exe, [string]$WorkDir, [string[]]$ArgumentList, [string]$StdoutPath, [string]$StderrPath) {
  $script:WinStdoutPath = $StdoutPath
  $script:WinStderrPath = $StderrPath
  [System.IO.File]::WriteAllText($StdoutPath, "", $Utf8NoBom)
  [System.IO.File]::WriteAllText($StderrPath, "", $Utf8NoBom)

  $proc = Start-Process -FilePath $Exe `
    -WorkingDirectory $WorkDir `
    -ArgumentList $ArgumentList `
    -RedirectStandardOutput $StdoutPath `
    -RedirectStandardError $StderrPath `
    -NoNewWindow `
    -PassThru
  if ($null -eq $proc) { throw "Failed to start Windows process" }
  $script:WinProc = $proc
  return $proc
}
function Stop-WindowsChat {
  if ($null -ne $script:WinProc -and -not $script:WinProc.HasExited) {
    Stop-Process -Id $script:WinProc.Id -Force -ErrorAction SilentlyContinue
    try { $script:WinProc.WaitForExit(15000) | Out-Null } catch { $null = $_ }
  }
  Get-Process win32_single_client_chat -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
  $script:WinProc = $null
}
function Disable-AndroidNet([string]$Adb, [string]$DeviceSerial) {
  Write-Host "Disabling Android emulator network"
  foreach ($c in @(
    @("shell", "cmd", "connectivity", "airplane-mode", "enable"),
    @("shell", "svc", "wifi", "disable"),
    @("shell", "svc", "data", "disable"),
    @("shell", "settings", "put", "global", "airplane_mode_on", "1"),
    @("shell", "am", "broadcast", "-a", "android.intent.action.AIRPLANE_MODE", "--ez", "state", "true")
  )) {
    try { Invoke-Adb $Adb $DeviceSerial $c | Out-Null } catch { Write-Host "  warn $($c -join ' ')" }
  }
  $script:NetworkDisabled = $true
}
function Enable-AndroidNet([string]$Adb, [string]$DeviceSerial) {
  Write-Host "Enabling Android emulator network"
  foreach ($c in @(
    @("shell", "cmd", "connectivity", "airplane-mode", "disable"),
    @("shell", "svc", "data", "enable"),
    @("shell", "svc", "wifi", "enable"),
    @("shell", "settings", "put", "global", "airplane_mode_on", "0"),
    @("shell", "am", "broadcast", "-a", "android.intent.action.AIRPLANE_MODE", "--ez", "state", "false")
  )) {
    try { Invoke-Adb $Adb $DeviceSerial $c | Out-Null } catch { Write-Host "  warn $($c -join ' ')" }
  }
  $script:NetworkDisabled = $false
}
function Wait-AndroidValidatedConnectivity([string]$Adb, [string]$DeviceSerial, [int]$TimeoutSec = 60) {
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $prev = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $out = & $Adb -s $DeviceSerial shell "ping -c 1 -W 1 8.8.8.8" 2>&1 | ForEach-Object { "$_" } | Out-String
    $ErrorActionPreference = $prev
    if ($out -match "1 (packets )?received|1 received") {
      $now = Get-Date
      Write-Host "  OK  validated connectivity (ICMP 8.8.8.8) at $($now.ToString('o'))"
      return $now
    }
    Start-Sleep -Milliseconds 250
  }
  throw "Timed out waiting for validated Android connectivity (ping 8.8.8.8)"
}

function Get-UiDump([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "uiautomator", "dump", "/sdcard/apptraverse_ui.xml") | Out-Null
  return Invoke-Adb $Adb $DeviceSerial @("shell", "cat", "/sdcard/apptraverse_ui.xml")
}
function Get-UiNode([string]$Xml, [string]$ResourceId) {
  if ([string]::IsNullOrEmpty($Xml)) { return $null }
  $match = [regex]::Match($Xml, '<node\b[^>]*resource-id="' + [regex]::Escape($ResourceId) + '"[^>]*>')
  if (-not $match.Success) { return $null }
  $node = $match.Value
  $text = ""; $x = 0; $y = 0
  if ($node -match '\btext="([^"]*)"') { $text = $Matches[1] }
  if ($node -match 'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"') {
    $x = [int](([int]$Matches[1] + [int]$Matches[3]) / 2)
    $y = [int](([int]$Matches[2] + [int]$Matches[4]) / 2)
  }
  return [pscustomobject]@{ Text = $text; X = $x; Y = $y }
}
function Send-AndroidMessage([string]$Adb, [string]$DeviceSerial, [string]$Text) {
  Start-AppNoWait $Adb $DeviceSerial
  Start-Sleep -Seconds 1
  for ($attempt = 1; $attempt -le 3; $attempt++) {
    $xml = Get-UiDump $Adb $DeviceSerial
    $input = Get-UiNode $xml "$PackageName`:id/message_input"
    if ($null -eq $input -or $input.X -le 0) { throw "Unable to locate message_input" }
    Invoke-Adb $Adb $DeviceSerial @("shell", "input", "tap", "$($input.X)", "$($input.Y)") | Out-Null
    Start-Sleep -Milliseconds 400
    $clear = @("shell", "input", "keyevent", "123") + (1..64 | ForEach-Object { "67" })
    Invoke-Adb $Adb $DeviceSerial $clear | Out-Null
    Start-Sleep -Milliseconds 200
    Invoke-Adb $Adb $DeviceSerial @("shell", "input", "text", $Text) | Out-Null
    Start-Sleep -Milliseconds 500
    $xml = Get-UiDump $Adb $DeviceSerial
    $typed = Get-UiNode $xml "$PackageName`:id/message_input"
    if ($null -ne $typed -and $typed.Text -eq $Text) {
      $send = Get-UiNode $xml "$PackageName`:id/send"
      if ($null -ne $send -and $send.X -gt 0) {
        Invoke-Adb $Adb $DeviceSerial @("shell", "input", "tap", "$($send.X)", "$($send.Y)") | Out-Null
      } else {
        Invoke-Adb $Adb $DeviceSerial @("shell", "input", "keyevent", "66") | Out-Null
      }
      Write-Host "  OK  Android sent '$Text'"
      return
    }
    Write-Host "  WARN Android type attempt $attempt"
    Start-Sleep -Seconds 1
  }
  throw "Failed to send Android message $Text"
}
function Send-WindowsInboxMessage([string]$Inbox, [string]$Key) {
  if ($null -eq $script:WinProc -or $script:WinProc.HasExited) { throw "Windows not running for $Key" }
  Remove-Item $Inbox -ErrorAction SilentlyContinue
  $tmp = "$Inbox.tmp"
  [System.IO.File]::WriteAllText($tmp, $Key)
  Move-Item -Force $tmp $Inbox
}
function Add-PeerViaInbox([string]$Inbox, [string]$Uid) {
  if ($null -eq $script:WinProc -or $script:WinProc.HasExited) { throw "Windows not running for peer-inbox" }
  Remove-Item $Inbox -ErrorAction SilentlyContinue
  $tmp = "$Inbox.tmp"
  [System.IO.File]::WriteAllText($tmp, $Uid)
  Move-Item -Force $tmp $Inbox
}
function Count-Matches([string]$Text, [string]$Pattern) {
  return ([regex]::Matches($Text, $Pattern)).Count
}
function Get-FieldFromPending([string]$Text) {
  $m = [regex]::Matches($Text, "CHAT_PENDING_CHANGED[^\r\n]*pending=(\d+)")
  if ($m.Count -eq 0) { return $null }
  return [int]$m[$m.Count - 1].Groups[1].Value
}

$repo_root = Resolve-RepoRoot
$sdk = Resolve-AndroidSdk
$adb = Get-Tool $sdk "platform-tools\adb.exe"
if (-not $AndroidDir) { $AndroidDir = Join-Path $repo_root "examples\single_client_chat\android" }
if (-not $WindowsBuildDir) { $WindowsBuildDir = Join-Path $repo_root "build-ogv2" }
$out_dir = Join-Path $repo_root "build\wifi-recovery"
$win_state_dir = Join-Path $out_dir "windows-state"
$commit_inbox = Join-Path $out_dir "windows_commit_inbox.txt"
$peer_inbox = Join-Path $out_dir "windows_peer_inbox.txt"
$stdout_log = Join-Path $out_dir "windows.stdout.log"
$stderr_log = Join-Path $out_dir "windows.stderr.log"
$summary_path = Join-Path $out_dir "summary.txt"

New-Item -ItemType Directory -Force -Path $out_dir | Out-Null
if (-not $env:CPM_SOURCE_CACHE) { $env:CPM_SOURCE_CACHE = "C:\cpm-cache" }

function Save-Artifacts([string]$Reason) {
  Write-Host "Saving artifacts ($Reason)"
  try { Write-Utf8NoBom (Join-Path $out_dir "android.logcat.txt") (Get-Logcat $adb $Serial) } catch { $null = $_ }
  try { Write-Utf8NoBom (Join-Path $out_dir "windows_memory.log") (Get-WindowsLogText) } catch { $null = $_ }
  $meta = @"
reason=$Reason
result=$($script:Result)
detail=$($script:Detail)
windows_uid=$($script:WindowsUid)
android_uid=$($script:AndroidUid)
expected_sha=$ExpectedAetherSha
"@
  Write-Utf8NoBom (Join-Path $out_dir "artifacts_meta.txt") $meta
}

try {
  Write-Host "=== CLEAN BUILD CACHES (known paths only) ==="
  if (-not $SkipWindowsBuild) {
    if (Test-Path $WindowsBuildDir) {
      Write-Host "  Removing $WindowsBuildDir"
      Remove-Item -Recurse -Force $WindowsBuildDir
    }
  }
  if (-not $SkipAndroidBuild) {
    foreach ($p in @(
      (Join-Path $AndroidDir "app\.cxx"),
      (Join-Path $AndroidDir "app\build")
    )) {
      if (Test-Path $p) {
        Write-Host "  Removing $p"
        Remove-Item -Recurse -Force $p
      }
    }
  }
  # Safe: only wipe aether-client-cpp CPM entries so the pin is re-fetched.
  if ((-not $SkipWindowsBuild) -or (-not $SkipAndroidBuild)) {
    $aether_cpm = Join-Path $env:CPM_SOURCE_CACHE "aether-client-cpp"
    if (Test-Path $aether_cpm) {
      Write-Host "  Removing CPM aether-client-cpp cache $aether_cpm"
      Remove-Item -Recurse -Force $aether_cpm
    }
  }

  $devices = Get-AdbDevices $adb
  if ($Serial) {
    if ($devices -notcontains $Serial) { throw "Requested device '$Serial' is not connected" }
  } else {
    $Serial = $devices | Where-Object { (& $adb -s $_ shell getprop ro.product.cpu.abi).Trim() -eq $Abi } | Select-Object -First 1
    if (-not $Serial) { throw "No connected Android device with ABI $Abi" }
  }
  Write-Host "Device serial        : $Serial"

  if (-not $SkipWindowsBuild) {
    Write-Host "Configuring Windows MSVC Debug (default remote pin, NO local CPM override)"
    New-Item -ItemType Directory -Force -Path $WindowsBuildDir | Out-Null
    cmake -S $repo_root -B $WindowsBuildDir -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
    cmake --build $WindowsBuildDir --config Debug --target win32_single_client_chat
    if ($LASTEXITCODE -ne 0) { throw "Windows build failed" }
  }
  if (-not $ExePath) {
    $ExePath = Join-Path $WindowsBuildDir "examples\single_client_chat\windows\Debug\win32_single_client_chat.exe"
  }
  if (-not (Test-Path $ExePath)) { throw "Windows exe not found: $ExePath" }

  if (-not $SkipAndroidBuild) {
    Ensure-JavaHome
    $env:ANDROID_SDK_ROOT = $sdk
    $env:ANDROID_HOME = $sdk
    Write-Host "Building Android assembleDebug (default pin)"
    Push-Location $AndroidDir
    try {
      & cmd /c ".\gradlew.bat -Papptraverse.abiFilters=$Abi :app:assembleDebug"
      if ($LASTEXITCODE -ne 0) { throw "Gradle assembleDebug failed ($LASTEXITCODE)" }
    } finally { Pop-Location }
  }
  if (-not $ApkPath) {
    $ApkPath = Join-Path $AndroidDir "app\build\outputs\apk\debug\app-debug.apk"
  }
  if (-not (Test-Path $ApkPath)) { throw "APK not found: $ApkPath" }
  if (-not $SkipInstall) {
    Invoke-Adb $adb $Serial @("install", "-r", "-t", "-g", $ApkPath) | Out-Null
  }

  Write-Host "=== FRESH ISOLATED STATES ==="
  Stop-WindowsChat
  Stop-App $adb $Serial
  # Ensure network on before start
  try { Enable-AndroidNet $adb $Serial } catch { $null = $_ }
  Invoke-Adb $adb $Serial @("shell", "pm", "clear", $PackageName) | Out-Null
  if (Test-Path $win_state_dir) { Remove-Item -Recurse -Force $win_state_dir }
  New-Item -ItemType Directory -Force -Path $win_state_dir | Out-Null
  Remove-Item $commit_inbox, $peer_inbox -ErrorAction SilentlyContinue
  & $ExePath --distill --state-dir $win_state_dir --aether-client-name $WindowsClientName
  if ($LASTEXITCODE -ne 0) { throw "Windows distill failed" }

  Write-Host "=== VERSION GATE ==="
  Clear-Logcat $adb $Serial
  Start-App $adb $Serial
  $aready = Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=" "Android AETHER_CLIENT_READY" $ClientReadyTimeoutSec
  $script:AndroidUid = Get-UidFromMarker $aready "android"
  $abuild_line = Wait-Marker $adb $Serial "AETHER_BUILD_INFO platform=android" "Android AETHER_BUILD_INFO" 60
  $abuild = Parse-BuildInfo $abuild_line
  Assert-BuildInfoMatches $abuild "android"
  Wait-Marker $adb $Serial "CHAT_SYNC_CONTROLLER_READY" "Android sync ready" 60 | Out-Null
  Write-Host "  Android BUILD_INFO: $($abuild.Line)"

  $win_args = @(
    "--state-dir", $win_state_dir,
    "--aether-client-name", $WindowsClientName,
    "--auto-accept-peer",
    "--commit-inbox", $commit_inbox,
    "--peer-inbox", $peer_inbox
  )
  Remove-Item $stdout_log, $stderr_log -ErrorAction SilentlyContinue
  $null = Start-WindowsRedirectedProcess $ExePath $repo_root $win_args $stdout_log $stderr_log
  $wready = Wait-WindowsMarker $script:WinProc "AETHER_CLIENT_READY platform=windows uid=" "Windows AETHER_CLIENT_READY" $ClientReadyTimeoutSec
  $script:WindowsUid = Get-UidFromMarker $wready "windows"
  $wbuild_line = Wait-WindowsMarker $script:WinProc "AETHER_BUILD_INFO platform=windows" "Windows AETHER_BUILD_INFO" 60
  $wbuild = Parse-BuildInfo $wbuild_line
  Assert-BuildInfoMatches $wbuild "windows"
  if ($wbuild.QuarantineMs -ne $abuild.QuarantineMs) {
    throw "AETHER_VERSION_MISMATCH quarantine_ms windows=$($wbuild.QuarantineMs) android=$($abuild.QuarantineMs)"
  }
  Write-Host "  Windows BUILD_INFO: $($wbuild.Line)"
  Write-Utf8NoBom (Join-Path $out_dir "aether_build_info.txt") ("windows=" + $wbuild.Line + "`r`nandroid=" + $abuild.Line + "`r`n")

  Write-Host "=== PAIRING ==="
  Add-PeerViaInbox $peer_inbox $script:AndroidUid
  Wait-WindowsMarker $script:WinProc "CHAT_PEER_INBOX_ADDED uid=$([regex]::Escape($script:AndroidUid))" "Windows peer added" $SyncTimeoutSec | Out-Null
  Wait-WindowsMarker $script:WinProc "CHAT_SYNC_INITIAL_COMPLETE|CHAT_PEER_ONLINE|P2P_SESSION_CREATED" "Windows session progress" $SyncTimeoutSec | Out-Null
  Wait-Marker $adb $Serial "CHAT_SYNC_INITIAL_COMPLETE|CHAT_PEER_ONLINE|P2P_SESSION_CREATED|CHAT_PEER_ADDED" "Android session progress" $SyncTimeoutSec | Out-Null

  Write-Host "=== BASELINE ==="
  Send-WindowsInboxMessage $commit_inbox "wifi_base_w_to_a"
  Wait-Marker $adb $Serial "CHAT_MESSAGE_VISIBLE platform=android text_key=wifi_base_w_to_a|TRANSCRIPT_PUBLISHED .*wifi_base_w_to_a" "Android saw wifi_base_w_to_a" $SyncTimeoutSec | Out-Null
  Wait-WindowsMarker $script:WinProc "CHAT_PENDING_CHANGED .*pending=0|SYNC_PENDING_REMOVED .*pending=0" "Windows pending=0 after baseline W->A" $SyncTimeoutSec | Out-Null

  Send-AndroidMessage $adb $Serial "wifi_base_a_to_w"
  Wait-WindowsMarker $script:WinProc "CHAT_MESSAGE_VISIBLE platform=windows text_key=wifi_base_a_to_w" "Windows saw wifi_base_a_to_w" $SyncTimeoutSec | Out-Null
  try { Wait-Marker $adb $Serial "CHAT_PENDING_CHANGED .*pending=0|SYNC_PENDING_REMOVED .*pending=0" "Android pending=0 after baseline A->W" $SyncTimeoutSec | Out-Null } catch { Write-Host "  WARN $($_.Exception.Message)" }

  $wlog = Get-WindowsLogText
  $alog = Get-Logcat $adb $Serial
  $w_sessions = Count-Matches $wlog "P2P_SESSION_CREATED "
  $a_sessions = Count-Matches $alog "P2P_SESSION_CREATED "
  if ($w_sessions -ne 1) { throw "Windows P2P_SESSION_CREATED=$w_sessions expected 1" }
  if ($a_sessions -ne 1) { throw "Android P2P_SESSION_CREATED=$a_sessions expected 1" }
  Write-Host "  OK  baseline both directions; P2P_SESSION_CREATED W=$w_sessions A=$a_sessions"

  Write-Host "=== OUTAGE (Android network only) ==="
  Disable-AndroidNet $adb $Serial
  Wait-WindowsMarker $script:WinProc "CHAT_PEER_OFFLINE peer=$([regex]::Escape($script:AndroidUid))" "Windows CHAT_PEER_OFFLINE" $OfflineTimeoutSec | Out-Null
  if ($script:WinProc.HasExited) { throw "Windows died during outage" }

  Send-WindowsInboxMessage $commit_inbox "wifi_w_to_a"
  Wait-WindowsMarker $script:WinProc "CHAT_MESSAGE_COMMITTED platform=windows .*text_key=wifi_w_to_a|MESSAGE_COMMITTED text=wifi_w_to_a" "Windows committed wifi_w_to_a" 60 | Out-Null
  Wait-WindowsMarker $script:WinProc "CHAT_PENDING_CHANGED .*pending=[1-9]|SYNC_PACKET_CREATED kind=event" "Windows pending>0 for wifi_w_to_a" 60 | Out-Null
  $w_event = ($null)
  $wlog = Get-WindowsLogText
  if ($wlog -match "CHAT_MESSAGE_COMMITTED platform=windows event=(\d+) text_key=wifi_w_to_a") {
    $w_event = $Matches[1]
  }
  $w_packet = $null
  if ($w_event -and $wlog -match "SYNC_PACKET_CREATED kind=event packet=(\d+) event=$([regex]::Escape($w_event))") {
    $w_packet = $Matches[1]
  }
  Write-Host "  Windows wifi_w_to_a event=$w_event packet=$w_packet"

  Send-AndroidMessage $adb $Serial "wifi_a_to_w"
  Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=wifi_a_to_w|CHAT_MESSAGE_COMMITTED platform=android .*text_key=wifi_a_to_w" "Android committed wifi_a_to_w" 60 | Out-Null
  try { Wait-Marker $adb $Serial "CHAT_PENDING_CHANGED .*pending=[1-9]|SYNC_PACKET_CREATED kind=event" "Android pending>0 for wifi_a_to_w" 60 | Out-Null } catch { Write-Host "  WARN $($_.Exception.Message)" }
  $alog = Get-Logcat $adb $Serial
  $a_event = $null; $a_packet = $null
  if ($alog -match "CHAT_MESSAGE_COMMITTED platform=android event=(\d+) text_key=wifi_a_to_w") { $a_event = $Matches[1] }
  elseif ($alog -match "MESSAGE_COMMITTED text=wifi_a_to_w") { $a_event = "unknown" }
  if ($a_event -and $a_event -ne "unknown" -and $alog -match "SYNC_PACKET_CREATED kind=event packet=(\d+) event=$([regex]::Escape($a_event))") {
    $a_packet = $Matches[1]
  }
  Write-Host "  Android wifi_a_to_w event=$a_event packet=$a_packet"

  Start-Sleep -Seconds $OutageSec
  # Confirm not yet delivered
  $alog_out = Get-Logcat $adb $Serial
  if ((Count-Matches $alog_out "CHAT_MESSAGE_VISIBLE platform=android text_key=wifi_w_to_a") -gt 0) {
    throw "wifi_w_to_a visible on Android during outage"
  }
  $wlog_out = Get-WindowsLogText
  if ((Count-Matches $wlog_out "CHAT_MESSAGE_VISIBLE platform=windows text_key=wifi_a_to_w") -gt 0) {
    throw "wifi_a_to_w visible on Windows during outage"
  }

  Write-Host "=== RESTORE ==="
  $network_command_time = Get-Date
  Enable-AndroidNet $adb $Serial
  $network_available_time = Wait-AndroidValidatedConnectivity $adb $Serial 90
  $net_avail_us = [int64]([DateTimeOffset]$network_available_time).ToUnixTimeMilliseconds() * 1000

  $delivery_deadline = $network_available_time.AddMilliseconds([Math]::Max($DeliveryTimeoutMs, 5000))
  # Allow a bit more wall time to observe, but PASS requires <= DeliveryTimeoutMs from network_available_time using marker t_us.
  $observe_deadline = $network_available_time.AddSeconds(30)
  $a_visible_us = $null
  $w_visible_us = $null
  while ((Get-Date) -lt $observe_deadline) {
    $alog = Get-Logcat $adb $Serial
    $wlog = Get-WindowsLogText
    if ($null -eq $a_visible_us) {
      $m = [regex]::Match($alog, "CHAT_MESSAGE_VISIBLE platform=android text_key=wifi_w_to_a t_us=(\d+)")
      if ($m.Success) { $a_visible_us = [int64]$m.Groups[1].Value }
    }
    if ($null -eq $w_visible_us) {
      $m = [regex]::Match($wlog, "CHAT_MESSAGE_VISIBLE platform=windows text_key=wifi_a_to_w t_us=(\d+)")
      if ($m.Success) { $w_visible_us = [int64]$m.Groups[1].Value }
    }
    if ($null -ne $a_visible_us -and $null -ne $w_visible_us) { break }
    if ($script:WinProc.HasExited) { throw "Windows died during restore" }
    Start-Sleep -Milliseconds 100
  }
  if ($null -eq $a_visible_us) { throw "Android never showed wifi_w_to_a after restore" }
  if ($null -eq $w_visible_us) { throw "Windows never showed wifi_a_to_w after restore" }

  $lat_a = [int64](($a_visible_us - $net_avail_us) / 1000)
  $lat_w = [int64](($w_visible_us - $net_avail_us) / 1000)
  $lat_max = [Math]::Max($lat_a, $lat_w)
  Write-Host "  latency_ms android_rx_wifi_w_to_a=$lat_a windows_rx_wifi_a_to_w=$lat_w max=$lat_max (budget=$DeliveryTimeoutMs)"

  # Wait pending clear
  Wait-WindowsMarker $script:WinProc "CHAT_PENDING_CHANGED .*pending=0|SYNC_PENDING_REMOVED .*pending=0" "Windows pending=0 after restore" $SyncTimeoutSec | Out-Null
  try { Wait-Marker $adb $Serial "CHAT_PENDING_CHANGED .*pending=0|SYNC_PENDING_REMOVED .*pending=0" "Android pending=0 after restore" $SyncTimeoutSec | Out-Null } catch { Write-Host "  WARN $($_.Exception.Message)" }

  $wlog = Get-WindowsLogText
  $alog = Get-Logcat $adb $Serial
  $a_vis = Count-Matches $alog "CHAT_MESSAGE_VISIBLE platform=android text_key=wifi_w_to_a"
  $w_vis = Count-Matches $wlog "CHAT_MESSAGE_VISIBLE platform=windows text_key=wifi_a_to_w"
  if ($a_vis -ne 1) { throw "Android wifi_w_to_a visible count=$a_vis want 1" }
  if ($w_vis -ne 1) { throw "Windows wifi_a_to_w visible count=$w_vis want 1" }

  $a_apply = 0
  if ($w_packet) { $a_apply = Count-Matches $alog "SYNC_EVENT_APPLIED packet=$([regex]::Escape($w_packet))" }
  else { $a_apply = Count-Matches $alog "SYNC_EVENT_APPLIED" }
  if ($a_apply -lt 1) { Write-Host "  WARN could not confirm exact apply count for wifi_w_to_a" }

  $w_sessions2 = Count-Matches $wlog "P2P_SESSION_CREATED "
  $a_sessions2 = Count-Matches $alog "P2P_SESSION_CREATED "
  if ($w_sessions2 -ne 1) { throw "Windows P2P_SESSION_CREATED became $w_sessions2" }
  if ($a_sessions2 -ne 1) { throw "Android P2P_SESSION_CREATED became $a_sessions2" }
  if ($wlog -match "CHAT_RECONNECT|P2P_SESSION_RECONNECTED" -or $alog -match "CHAT_RECONNECT|P2P_SESSION_RECONNECTED") {
    throw "Unexpected CHAT_RECONNECT / P2P_SESSION_RECONNECTED"
  }
  if ($wlog -match "(?i)restream" -or $alog -match "(?i)restream") {
    throw "Unexpected Restream mention"
  }
  if ($lat_max -gt $DeliveryTimeoutMs) {
    throw "Delivery latency ${lat_max}ms > ${DeliveryTimeoutMs}ms after network_available_time"
  }

  $script:Result = "PASS"
  $script:Detail = "latency_ms=$lat_max w_event=$w_event w_packet=$w_packet a_event=$a_event a_packet=$a_packet"
  $summary = @"
result=PASS
latency_ms=$lat_max
latency_android_ms=$lat_a
latency_windows_ms=$lat_w
network_command_time=$($network_command_time.ToString('o'))
network_available_time=$($network_available_time.ToString('o'))
windows_uid=$($script:WindowsUid)
android_uid=$($script:AndroidUid)
windows_build_info=$($wbuild.Line)
android_build_info=$($abuild.Line)
wifi_w_to_a_event=$w_event
wifi_w_to_a_packet=$w_packet
wifi_a_to_w_event=$a_event
wifi_a_to_w_packet=$a_packet
p2p_session_created_windows=$w_sessions2
p2p_session_created_android=$a_sessions2
"@
  Write-Utf8NoBom $summary_path $summary
  Write-Host ""
  Write-Host "WIFI_RECOVERY PASS latency_ms=$lat_max"
  exit 0
}
catch {
  $script:Result = "FAIL"
  $script:Detail = $_.Exception.Message
  Write-Host "WIFI_RECOVERY FAIL: $($_.Exception.Message)"
  Save-Artifacts "fail"
  $fail = @"
result=FAIL
detail=$($script:Detail)
windows_uid=$($script:WindowsUid)
android_uid=$($script:AndroidUid)
"@
  Write-Utf8NoBom $summary_path $fail
  exit 1
}
finally {
  if ($script:NetworkDisabled) {
    try { Enable-AndroidNet $adb $Serial } catch { $null = $_ }
  }
  Stop-WindowsChat
}
