# Windows <-> Android directional delivery and offline-recovery smoke.
#
# Measures online delivery in both directions, then proves that packets survive
# a sender restart while the receiver is offline.  Artifacts are written below
# build/directional-smoke (which is intentionally ignored).

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
  [int]$OnlineMessageCount = 5,
  [int]$MaxOnlineDeliverySec = 15,
  [int]$OfflineRecoveryTimeoutSec = 45
)

$ErrorActionPreference = "Stop"
$PackageName = "com.apptraverse.singleclientchat"
$ActivityName = "$PackageName/.MainActivity"
$WindowsClientName = "apptraverse-windows-directional"
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Resolve-RepoRoot {
  $script_dir = Split-Path -Parent $PSCommandPath
  return (Resolve-Path (Join-Path $script_dir "..\..")).Path
}
function Resolve-AndroidSdk {
  if ($env:ANDROID_SDK_ROOT -and (Test-Path $env:ANDROID_SDK_ROOT)) { return (Resolve-Path $env:ANDROID_SDK_ROOT).Path }
  if ($env:ANDROID_HOME -and (Test-Path $env:ANDROID_HOME)) { return (Resolve-Path $env:ANDROID_HOME).Path }
  $default = Join-Path $env:LOCALAPPDATA "Android\Sdk"
  if (Test-Path $default) { return (Resolve-Path $default).Path }
  throw "Android SDK not found. Set ANDROID_SDK_ROOT or install the Android SDK."
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
  if ($code -ne 0) { throw "adb $($Arguments -join ' ') failed with exit code $code`n$output" }
  return $output
}
function Clear-Logcat([string]$Adb, [string]$DeviceSerial) {
  & $Adb -s $DeviceSerial logcat -c 2>&1 | Out-Null
}
function Get-Logcat([string]$Adb, [string]$DeviceSerial) {
  return (& $Adb -s $DeviceSerial logcat -d -v brief 2>&1 | ForEach-Object { "$_" } | Out-String)
}
function Get-WindowsLog([string]$Path) {
  if (-not (Test-Path $Path)) { return "" }
  $text = Get-Content -Raw -Path $Path -ErrorAction SilentlyContinue
  return $(if ($null -eq $text) { "" } else { $text })
}
function Wait-Marker([string]$Adb, [string]$DeviceSerial, [string]$Pattern,
                     [string]$Description, [int]$TimeoutSec = 90) {
  $deadline = (Get-Date).AddSeconds($TimeoutSec); $logs = ""
  while ((Get-Date) -lt $deadline) {
    $logs = Get-Logcat $Adb $DeviceSerial
    $match = ($logs -split "`n" | Where-Object { $_ -match $Pattern } | Select-Object -Last 1)
    if ($match) { Write-Host "  OK  $Description"; return $match.Trim() }
    Start-Sleep -Milliseconds 350
  }
  throw "Timed out waiting for $Description (pattern: $Pattern)"
}
function Wait-WindowsMarker([System.Diagnostics.Process]$Process, [string]$LogPath,
                            [string]$Pattern, [string]$Description, [int]$TimeoutSec) {
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $text = Get-WindowsLog $LogPath
    $match = ($text -split "`n" | Where-Object { $_ -match $Pattern } | Select-Object -Last 1)
    if ($match) { Write-Host "  OK  $Description"; return $match.Trim() }
    if ($null -ne $Process -and $Process.HasExited) {
      throw "Windows process exited early (code=$($Process.ExitCode)) while waiting for $Description`n$text"
    }
    Start-Sleep -Milliseconds 300
  }
  throw "Timed out waiting for Windows $Description (pattern: $Pattern)`n$(Get-WindowsLog $LogPath)"
}
function Start-WindowsRedirected([string]$Exe, [string]$WorkingDir, [string]$Arguments, [string]$LogPath) {
  # cmd redirection keeps a single combined log; always pair with Stop-WindowsChat
  # so orphaned win32_single_client_chat processes cannot survive the wrapper.
  $cmd = "cd /d `"$WorkingDir`" && `"$Exe`" $Arguments > `"$LogPath`" 2>&1"
  return Start-Process -FilePath "cmd.exe" -ArgumentList @("/c", $cmd) -PassThru -WindowStyle Hidden
}
function Stop-WindowsChat {
  Get-Process win32_single_client_chat -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 500
}
function Stop-WindowsRun([System.Diagnostics.Process]$Process) {
  if ($null -ne $Process -and -not $Process.HasExited) {
    Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    try { $Process.WaitForExit(15000) | Out-Null } catch {}
  }
  Stop-WindowsChat
  Start-Sleep -Seconds 2
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
function Ensure-JavaHome {
  if ($env:JAVA_HOME -and (Test-Path $env:JAVA_HOME)) { return }
  $jdk20 = "C:\Program Files\Java\jdk-20"
  $jbr = Join-Path ${env:ProgramFiles} "Android\Android Studio\jbr"
  if (Test-Path $jdk20) { $env:JAVA_HOME = $jdk20 }
  elseif (Test-Path $jbr) { $env:JAVA_HOME = $jbr }
  else { throw "No JDK found. Set JAVA_HOME to JDK 17-23." }
}
function Get-UiDump([string]$Adb, [string]$DeviceSerial) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "uiautomator", "dump", "/sdcard/apptraverse_ui.xml") | Out-Null
  return Invoke-Adb $Adb $DeviceSerial @("shell", "cat", "/sdcard/apptraverse_ui.xml")
}
function Get-UiNode([string]$Xml, [string]$ResourceId) {
  $match = [regex]::Match($Xml, '<node\b[^>]*resource-id="' + [regex]::Escape($ResourceId) + '"[^>]*>')
  if (-not $match.Success) { return $null }
  $node = $match.Value; $text = ""; $x = 0; $y = 0
  if ($node -match '\btext="([^"]*)"') { $text = $Matches[1] }
  if ($node -match 'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"') {
    $x = [int](([int]$Matches[1] + [int]$Matches[3]) / 2)
    $y = [int](([int]$Matches[2] + [int]$Matches[4]) / 2)
  }
  return [pscustomobject]@{ Text = $text; X = $x; Y = $y }
}
function Send-AndroidMessage([string]$Adb, [string]$DeviceSerial, [string]$Text) {
  Invoke-Adb $Adb $DeviceSerial @("shell", "am", "start", "-n", $ActivityName) | Out-Null
  Start-Sleep -Seconds 1
  for ($attempt = 1; $attempt -le 3; $attempt++) {
    $input = Get-UiNode (Get-UiDump $Adb $DeviceSerial) "$PackageName`:id/message_input"
    if ($null -eq $input -or $input.X -le 0) { throw "Unable to locate message_input" }
    Invoke-Adb $Adb $DeviceSerial @("shell", "input", "tap", "$($input.X)", "$($input.Y)") | Out-Null
    $clear = @("shell", "input", "keyevent", "123") + (1..64 | ForEach-Object { "67" })
    Invoke-Adb $Adb $DeviceSerial $clear | Out-Null
    Invoke-Adb $Adb $DeviceSerial @("shell", "input", "text", $Text) | Out-Null
    Start-Sleep -Milliseconds 600
    $typed = Get-UiNode (Get-UiDump $Adb $DeviceSerial) "$PackageName`:id/message_input"
    if ($null -ne $typed -and $typed.Text -eq $Text) {
      Invoke-Adb $Adb $DeviceSerial @("shell", "input", "keyevent", "66") | Out-Null
      Write-Host "  OK  Android sent '$Text'"; return
    }
  }
  throw "EditText does not contain expected text '$Text' before Send"
}
function Get-UidFromMarker([string]$Line, [string]$Platform) {
  if ($Line -notmatch "AETHER_CLIENT_READY platform=$Platform uid=([0-9a-fA-F-]+)") {
    throw "Unable to parse $Platform UID from: $Line"
  }
  return $Matches[1]
}
function Get-MarkerField([string]$Line, [string]$Name) {
  if ($Line -match "(?:^|\s)$([regex]::Escape($Name))=([^\s]+)") { return $Matches[1] }
  return $null
}
function Get-EventId([string]$Line) {
  foreach ($name in @("event", "event_id")) {
    $value = Get-MarkerField $Line $name
    if ($value) { return $value }
  }
  throw "Unable to parse event id from: $Line"
}
function Get-CommitEventId([string]$Text, [string]$Platform, [string]$Key) {
  # MESSAGE_COMMITTED is retained as a compatibility marker, but it deliberately
  # has no event field.  Always obtain the durable event id from the richer
  # CHAT_MESSAGE_COMMITTED line for subsequent packet lifecycle assertions.
  $pattern = "CHAT_MESSAGE_COMMITTED platform=$Platform .*text_key=$([regex]::Escape($Key))"
  $line = ($Text -split "`n" | Where-Object { $_ -match $pattern } | Select-Object -Last 1)
  if (-not $line) { throw "Unable to find CHAT_MESSAGE_COMMITTED for $Platform/$Key" }
  return Get-EventId $line.Trim()
}
function Get-PacketId([string]$Line) {
  foreach ($name in @("packet", "packet_id")) {
    $value = Get-MarkerField $Line $name
    if ($value) { return $value }
  }
  throw "Unable to parse packet id from: $Line"
}
function Get-PacketCreatePattern([string]$EventId) {
  return "SYNC_PACKET_CREATED .*?(?:event|event_id)=$([regex]::Escape($EventId))(?:\s|$)"
}
function Get-PacketPattern([string]$Marker, [string]$PacketId) {
  if ($Marker -eq "SYNC_ACK_RECEIVED" -or $Marker -eq "SYNC_ACK_SENT") {
    return "$Marker .*acknowledged=$([regex]::Escape($PacketId))(?:\s|$)"
  }
  return "$Marker .*?(?:packet|packet_id)=$([regex]::Escape($PacketId))(?:\s|$)"
}
function Save-Artifacts {
  $android_text = Get-Logcat $adb $Serial
  Write-Utf8NoBom (Join-Path $out_dir "android.logcat.txt") $android_text
  $all_windows = Get-ChildItem -Path $out_dir -Filter "windows_*.log" -ErrorAction SilentlyContinue |
    ForEach-Object { "===== $($_.Name) =====`r`n$(Get-WindowsLog $_.FullName)" }
  Write-Utf8NoBom (Join-Path $out_dir "windows_logs.txt") ($all_windows -join "`r`n")
}
function Dump-PacketLifecycle([string]$Key, [string]$Reason) {
  Save-Artifacts
  $lines = @("===== $Reason key=$Key =====")
  foreach ($source in @(
      [pscustomobject]@{ Name = "android"; Text = Get-Logcat $adb $Serial },
      [pscustomobject]@{ Name = "windows"; Text = (Get-ChildItem $out_dir -Filter "windows_*.log" | ForEach-Object { Get-WindowsLog $_.FullName }) -join "`n" })) {
    $hits = $source.Text -split "`n" | Where-Object {
      $_ -match [regex]::Escape($Key) -or $_ -match "SYNC_(PACKET|ACK|PENDING|EVENT|TRANSPORT)" -or $_ -match "CHAT_PENDING"
    }
    $lines += "----- $($source.Name) -----"; $lines += $hits
  }
  Add-Utf8NoBom $lifecycle_path (($lines -join "`r`n") + "`r`n")
  Write-Host ($lines -join "`n")
}
function Add-Lifecycle([string]$Key) {
  $lines = @("===== completed key=$Key =====")
  foreach ($source in @(
      [pscustomobject]@{ Name = "android"; Text = Get-Logcat $adb $Serial },
      [pscustomobject]@{ Name = "windows"; Text = (Get-ChildItem $out_dir -Filter "windows_*.log" | ForEach-Object { Get-WindowsLog $_.FullName }) -join "`n" })) {
    $lines += "----- $($source.Name) -----"
    $lines += ($source.Text -split "`n" | Where-Object {
      $_ -match [regex]::Escape($Key) -or $_ -match "SYNC_(PACKET|ACK|PENDING|EVENT|TRANSPORT)"
    })
  }
  Add-Utf8NoBom $lifecycle_path (($lines -join "`r`n") + "`r`n")
}
function Add-Latency([string]$Direction, [string]$Key, [long]$ElapsedMs) {
  $script:latencies += [pscustomobject]@{ Direction = $Direction; Key = $Key; ElapsedMs = $ElapsedMs }
  Add-Utf8NoBom $latency_path "$Direction,$Key,$ElapsedMs`r`n"
}
function Get-Median([long[]]$Values) {
  $sorted = @($Values | Sort-Object); $count = $sorted.Count
  if (($count % 2) -eq 1) { return [double]$sorted[[int]($count / 2)] }
  return ([double]$sorted[$count / 2 - 1] + [double]$sorted[$count / 2]) / 2
}
function Assert-PendingCleared([string]$Platform, [System.Diagnostics.Process]$WinProcess, [string]$WinLog, [int]$Timeout) {
  $pattern = "(CHAT_PENDING_CLEARED|SYNC_PENDING_REMOVED .*pending=0)"
  if ($Platform -eq "android") { Wait-Marker $adb $Serial $pattern "Android pending cleared" $Timeout | Out-Null }
  else { Wait-WindowsMarker $WinProcess $WinLog $pattern "Windows pending cleared" $Timeout | Out-Null }
}
function Send-WindowsMessage([string]$Key) {
  Stop-WindowsChat
  Start-Sleep -Seconds 2
  $log = Join-Path $out_dir "windows_commit_$Key.log"
  Remove-Item $log -ErrorAction SilentlyContinue
  $args = "--state-dir `"$win_state_dir`" --aether-client-name $WindowsClientName --peer $android_uid --auto-accept-peer --commit-message $Key --exit-after-pending-clear"
  $proc = Start-WindowsRedirected $win_exe $repo_root $args $log
  $commit = Wait-WindowsMarker $proc $log "CHAT_MESSAGE_COMMITTED platform=windows .*text_key=$([regex]::Escape($Key))|MESSAGE_COMMITTED text=$([regex]::Escape($Key))" "Windows committed $Key" 90
  return [pscustomobject]@{ Process = $proc; Log = $log; Commit = $commit }
}
function Send-WindowsInboxMessage([System.Diagnostics.Process]$Process, [string]$Log, [string]$Inbox, [string]$Key) {
  if ($null -eq $Process -or $Process.HasExited) { throw "Windows process is not running for inbox commit $Key" }
  Remove-Item $Inbox -ErrorAction SilentlyContinue
  # Atomic-ish write: temp then move so the poller never reads a partial line.
  $tmp = "$Inbox.tmp"
  [System.IO.File]::WriteAllText($tmp, $Key)
  Move-Item -Force $tmp $Inbox
  $commit = Wait-WindowsMarker $Process $Log "CHAT_MESSAGE_COMMITTED platform=windows .*text_key=$([regex]::Escape($Key))|MESSAGE_COMMITTED text=$([regex]::Escape($Key))" "Windows committed $Key" 90
  return $commit
}

$repo_root = Resolve-RepoRoot
$sdk = Resolve-AndroidSdk
$adb = Get-Tool $sdk "platform-tools\adb.exe"
$android_dir = Join-Path $repo_root "examples\single_client_chat\android"
$out_dir = Join-Path $repo_root "build\directional-smoke"
$win_state_dir = Join-Path $out_dir "windows-state"
$win_build_dir = Join-Path $repo_root "build-msvc"
$win_exe = Join-Path $win_build_dir "examples\single_client_chat\windows\Debug\win32_single_client_chat.exe"
$latency_path = Join-Path $out_dir "latency_summary.csv"
$lifecycle_path = Join-Path $out_dir "packet_lifecycle.txt"
$generations_path = Join-Path $out_dir "outgoing_generations.txt"
$script:latencies = @()

New-Item -ItemType Directory -Force -Path $out_dir | Out-Null
if (Test-Path $win_state_dir) { Remove-Item -Recurse -Force $win_state_dir }
New-Item -ItemType Directory -Force -Path $win_state_dir | Out-Null
Write-Utf8NoBom $latency_path "direction,key,elapsed_ms`r`n"
Write-Utf8NoBom $lifecycle_path ""
Write-Utf8NoBom $generations_path ""

try {
  if (-not $ApkPath) { $ApkPath = Join-Path $android_dir "app\build\outputs\apk\debug\app-debug.apk" }
  if (-not $env:CPM_SOURCE_CACHE) { $env:CPM_SOURCE_CACHE = "C:\cpm-cache" }
  $devices = Get-AdbDevices $adb
  if ($Serial) {
    if ($devices -notcontains $Serial) { throw "Requested device '$Serial' is not connected" }
  } else {
    $Serial = $devices | Where-Object { (& $adb -s $_ shell getprop ro.product.cpu.abi).Trim() -eq $Abi } | Select-Object -First 1
    if (-not $Serial) { throw "No connected Android device with ABI $Abi. Specify -Serial or start the matching emulator." }
  }
  Write-Host "Device serial        : $Serial"

  if (-not $SkipAndroidBuild) {
    Ensure-JavaHome; $env:ANDROID_SDK_ROOT = $sdk; $env:ANDROID_HOME = $sdk
    Push-Location $android_dir
    try {
      & cmd /c ".\gradlew.bat -Papptraverse.abiFilters=x86_64,arm64-v8a :app:assembleDebug"
      if ($LASTEXITCODE -ne 0) { throw "Gradle assembleDebug failed with exit code $LASTEXITCODE" }
    } finally { Pop-Location }
  }
  if (-not (Test-Path $ApkPath)) { throw "APK not found: $ApkPath" }
  if (-not $SkipInstall) { Invoke-Adb $adb $Serial @("install", "-r", "-t", "-g", $ApkPath) | Out-Null }
  Invoke-Adb $adb $Serial @("shell", "pm", "clear", $PackageName) | Out-Null

  if (-not $SkipWindowsBuild) {
    if (-not (Test-Path (Join-Path $win_build_dir "CMakeCache.txt"))) {
      cmake -S $repo_root -B $win_build_dir -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
      if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
    }
    cmake --build $win_build_dir --config Debug --target win32_single_client_chat
    if ($LASTEXITCODE -ne 0) { throw "Windows build failed" }
  }
  if (-not (Test-Path $win_exe)) { throw "Windows executable not found: $win_exe" }
  Stop-WindowsChat
  & $win_exe --distill --state-dir $win_state_dir --aether-client-name $WindowsClientName
  if ($LASTEXITCODE -ne 0) { throw "Windows --distill failed" }

  # Collect both durable identities before pairing.
  Stop-App $adb $Serial; Clear-Logcat $adb $Serial; Start-App $adb $Serial
  $android_uid = Get-UidFromMarker (Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=" "Android client ready" $ClientReadyTimeoutSec) "android"
  $uid_log = Join-Path $out_dir "windows_uid.log"
  $uid_proc = Start-WindowsRedirected $win_exe $repo_root "--state-dir `"$win_state_dir`" --aether-client-name $WindowsClientName --print-aether-uid" $uid_log
  try {
    $windows_uid = Get-UidFromMarker (Wait-WindowsMarker $uid_proc $uid_log "AETHER_CLIENT_READY platform=windows uid=" "Windows client ready" $ClientReadyTimeoutSec) "windows"
    Wait-WindowsMarker $uid_proc $uid_log "AETHER_UID=" "Windows UID printed" 30 | Out-Null
  } finally { Stop-WindowsRun $uid_proc }
  Write-Utf8NoBom (Join-Path $out_dir "uids.txt") "android_uid=$android_uid`r`nwindows_uid=$windows_uid`r`n"

  # Fresh pair and hold a long-running Windows endpoint for Android-originated delivery.
  Stop-App $adb $Serial; Clear-Logcat $adb $Serial; Start-App $adb $Serial
  $primary_log = Join-Path $out_dir "windows_primary.log"
  $commit_inbox = Join-Path $out_dir "windows_commit_inbox.txt"
  Remove-Item $commit_inbox -ErrorAction SilentlyContinue
  $primary_args = "--state-dir `"$win_state_dir`" --aether-client-name $WindowsClientName --peer $android_uid --auto-accept-peer --commit-inbox `"$commit_inbox`""
  $primary_args_no_inbox = "--state-dir `"$win_state_dir`" --aether-client-name $WindowsClientName --peer $android_uid --auto-accept-peer"
  $primary = Start-WindowsRedirected $win_exe $repo_root $primary_args $primary_log
  Wait-Marker $adb $Serial "CHAT_SYNC_INITIAL_COMPLETE" "Android initial sync" $SyncTimeoutSec | Out-Null
  Wait-WindowsMarker $primary $primary_log "CHAT_SYNC_INITIAL_COMPLETE" "Windows initial sync" $SyncTimeoutSec | Out-Null
  Wait-Marker $adb $Serial "P2P_OUTGOING_STATE peer=\S+.*state=writable" "Android outgoing writable" $SyncTimeoutSec | Out-Null
  Wait-WindowsMarker $primary $primary_log "P2P_OUTGOING_STATE peer=\S+.*state=writable" "Windows outgoing writable" $SyncTimeoutSec | Out-Null
  # Keep the paired Windows process alive for the whole online phase. Restarting
  # per w2a_* message made Android Restream/Replace its outgoing path and then
  # stall Android->Windows delivery.

  Write-Host "`nOnline Windows -> Android"
  for ($i = 1; $i -le $OnlineMessageCount; $i++) {
    $key = "w2a_$i"
    try {
      Send-WindowsInboxMessage $primary $primary_log $commit_inbox $key | Out-Null
      $event = Get-CommitEventId (Get-WindowsLog $primary_log) "windows" $key
      $created = Wait-WindowsMarker $primary $primary_log (Get-PacketCreatePattern $event) "Windows created packet for $key" 60
      $packet = Get-PacketId $created
      $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
      Wait-Marker $adb $Serial "CHAT_MESSAGE_VISIBLE platform=android text_key=$([regex]::Escape($key))" "Android saw $key" $MaxOnlineDeliverySec | Out-Null
      $stopwatch.Stop(); Add-Latency "windows_to_android" $key $stopwatch.ElapsedMilliseconds
      if ($stopwatch.Elapsed.TotalSeconds -gt $MaxOnlineDeliverySec) {
        throw "Delivery of $key took $($stopwatch.ElapsedMilliseconds) ms (> ${MaxOnlineDeliverySec}s)"
      }
      Wait-WindowsMarker $primary $primary_log (Get-PacketPattern "SYNC_ACK_RECEIVED" $packet) "Windows received ACK for $key" $SyncTimeoutSec | Out-Null
      Assert-PendingCleared "windows" $primary $primary_log $SyncTimeoutSec
      Add-Lifecycle $key
    } catch { Dump-PacketLifecycle $key $_.Exception.Message; throw }
  }

  Write-Host "`nOnline Android -> Windows"
  Wait-WindowsMarker $primary $primary_log "P2P_OUTGOING_STATE peer=\S+.*state=writable" "Windows outgoing writable for A2W" $SyncTimeoutSec | Out-Null
  Wait-Marker $adb $Serial "P2P_OUTGOING_STATE peer=\S+.*state=writable" "Android outgoing writable for A2W" $SyncTimeoutSec | Out-Null
  Start-Sleep -Seconds 1
  for ($i = 1; $i -le $OnlineMessageCount; $i++) {
    $key = "a2w_$i"
    try {
      Send-AndroidMessage $adb $Serial $key
      $commit = Wait-Marker $adb $Serial "CHAT_MESSAGE_COMMITTED platform=android .*text_key=$([regex]::Escape($key))|MESSAGE_COMMITTED text=$([regex]::Escape($key))" "Android committed $key" 60
      $event = Get-CommitEventId (Get-Logcat $adb $Serial) "android" $key
      $created = Wait-Marker $adb $Serial (Get-PacketCreatePattern $event) "Android created packet for $key" 60
      $packet = Get-PacketId $created
      Wait-Marker $adb $Serial "SYNC_TRANSPORT_WRITE .*packet=$([regex]::Escape($packet)).*generation=\d+" "Android transport write generation for $key" $SyncTimeoutSec | Out-Null
      $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
      Wait-WindowsMarker $primary $primary_log "CHAT_MESSAGE_VISIBLE platform=windows text_key=$([regex]::Escape($key))" "Windows saw $key" $MaxOnlineDeliverySec | Out-Null
      $stopwatch.Stop(); Add-Latency "android_to_windows" $key $stopwatch.ElapsedMilliseconds
      if ($stopwatch.Elapsed.TotalSeconds -gt $MaxOnlineDeliverySec) {
        throw "Delivery of $key took $($stopwatch.ElapsedMilliseconds) ms (> ${MaxOnlineDeliverySec}s)"
      }
      Wait-Marker $adb $Serial (Get-PacketPattern "SYNC_ACK_RECEIVED" $packet) "Android received ACK for $key" $SyncTimeoutSec | Out-Null
      Assert-PendingCleared "android" $primary $primary_log $SyncTimeoutSec
      Add-Lifecycle $key
    } catch { Dump-PacketLifecycle $key $_.Exception.Message; throw }
  }

  $w_latencies = @($latencies | Where-Object Direction -eq "windows_to_android" | ForEach-Object ElapsedMs)
  $a_latencies = @($latencies | Where-Object Direction -eq "android_to_windows" | ForEach-Object ElapsedMs)
  $w_median = Get-Median $w_latencies; $a_median = Get-Median $a_latencies
  $summary = @(
    "direction,min_ms,median_ms,max_ms",
    "windows_to_android,$(($w_latencies | Measure-Object -Minimum).Minimum),$w_median,$(($w_latencies | Measure-Object -Maximum).Maximum)",
    "android_to_windows,$(($a_latencies | Measure-Object -Minimum).Minimum),$a_median,$(($a_latencies | Measure-Object -Maximum).Maximum)"
  )
  Add-Utf8NoBom $latency_path (($summary -join "`r`n") + "`r`n")
  Write-Host ($summary -join "`n")
  if ($a_median -gt (10 * [Math]::Max(1, $w_median))) { throw "Android->Windows median ($a_median ms) exceeds 10x Windows->Android median ($w_median ms)" }

  # Android sender survives a force-stop while its Windows receiver is absent.
  Write-Host "`nAndroid -> offline Windows recovery"
  Stop-WindowsRun $primary
  $key = "android_offline_persist"
  try {
    Send-AndroidMessage $adb $Serial $key
    $commit = Wait-Marker $adb $Serial "CHAT_MESSAGE_COMMITTED platform=android .*text_key=$key|MESSAGE_COMMITTED text=$key" "Android committed offline packet" 60
    $event = Get-CommitEventId (Get-Logcat $adb $Serial) "android" $key
    $created = Wait-Marker $adb $Serial (Get-PacketCreatePattern $event) "Android created offline packet" 60
    $packet = Get-PacketId $created
    # Packet stays pending while Windows is down: created, no ACK yet.
    Start-Sleep -Seconds 1
    if ((Get-Logcat $adb $Serial) -match (Get-PacketPattern "SYNC_ACK_RECEIVED" $packet)) {
      throw "Android received ACK while Windows was offline"
    }
    if ((Get-Logcat $adb $Serial) -notmatch (Get-PacketPattern "SYNC_PACKET_CREATED" $packet)) {
      throw "Android offline packet create marker missing"
    }
    Stop-App $adb $Serial
    $recovery_log = Join-Path $out_dir "windows_android_offline_recovery.log"
    Remove-Item $recovery_log -ErrorAction SilentlyContinue
    $recovery = Start-WindowsRedirected $win_exe $repo_root $primary_args $recovery_log
    Start-App $adb $Serial
    Wait-Marker $adb $Serial "CHAT_SYNC_RESUMED" "Android resumed with pending packet" $OfflineRecoveryTimeoutSec | Out-Null
    Wait-Marker $adb $Serial (Get-PacketPattern "SYNC_PACKET_RETRY" $packet) "Android retried same packet" $OfflineRecoveryTimeoutSec | Out-Null
    Wait-WindowsMarker $recovery $recovery_log (Get-PacketPattern "SYNC_PACKET_RECEIVED" $packet) "Windows received offline packet" $OfflineRecoveryTimeoutSec | Out-Null
    Wait-WindowsMarker $recovery $recovery_log "SYNC_EVENT_APPLIED .*?(?:event|event_id)=$event" "Windows applied offline event" $OfflineRecoveryTimeoutSec | Out-Null
    Wait-WindowsMarker $recovery $recovery_log "CHAT_MESSAGE_VISIBLE platform=windows text_key=$key" "Windows saw offline Android message" $OfflineRecoveryTimeoutSec | Out-Null
    Wait-Marker $adb $Serial (Get-PacketPattern "SYNC_ACK_RECEIVED" $packet) "Android received recovered ACK" $OfflineRecoveryTimeoutSec | Out-Null
    Wait-Marker $adb $Serial "SYNC_PENDING_REMOVED .*pending=0|CHAT_PENDING_CLEARED" "Android cleared recovered pending" $OfflineRecoveryTimeoutSec | Out-Null
    if (([regex]::Matches((Get-WindowsLog $recovery_log), "CHAT_MESSAGE_VISIBLE platform=windows text_key=$key")).Count -ne 1) { throw "Windows showed $key more than once" }
    Add-Lifecycle $key
    Stop-WindowsRun $recovery
    $primary = $null
  } catch { Dump-PacketLifecycle $key $_.Exception.Message; throw }

  # Windows sender persists the reciprocal outage; it need not restart itself.
  Write-Host "`nWindows -> offline Android recovery"
  Stop-App $adb $Serial
  $key = "windows_offline_persist"; $offline_win = $null
  try {
    $offline_win = Send-WindowsMessage $key
    $event = Get-CommitEventId (Get-WindowsLog $offline_win.Log) "windows" $key
    $created = Wait-WindowsMarker $offline_win.Process $offline_win.Log (Get-PacketCreatePattern $event) "Windows created offline packet" 60
    $packet = Get-PacketId $created
    Start-Sleep -Seconds 1
    if ((Get-WindowsLog $offline_win.Log) -match (Get-PacketPattern "SYNC_ACK_RECEIVED" $packet)) {
      throw "Windows received ACK while Android was offline"
    }
    Start-App $adb $Serial
    Wait-Marker $adb $Serial "CHAT_SYNC_CONTROLLER_READY|CHAT_SYNC_RESUMED|CHAT_SYNC_INITIAL_COMPLETE" "Android ready for Windows recovery" $OfflineRecoveryTimeoutSec | Out-Null
    Wait-WindowsMarker $offline_win.Process $offline_win.Log (Get-PacketPattern "SYNC_PACKET_RETRY" $packet) "Windows retried same packet" $OfflineRecoveryTimeoutSec | Out-Null
    Wait-Marker $adb $Serial (Get-PacketPattern "SYNC_PACKET_RECEIVED" $packet) "Android received offline packet" $OfflineRecoveryTimeoutSec | Out-Null
    Wait-Marker $adb $Serial "SYNC_EVENT_APPLIED .*?(?:event|event_id)=$event" "Android applied offline event" $OfflineRecoveryTimeoutSec | Out-Null
    Wait-Marker $adb $Serial "CHAT_MESSAGE_VISIBLE platform=android text_key=$key" "Android saw offline Windows message" $OfflineRecoveryTimeoutSec | Out-Null
    Wait-WindowsMarker $offline_win.Process $offline_win.Log (Get-PacketPattern "SYNC_ACK_RECEIVED" $packet) "Windows received recovered ACK" $OfflineRecoveryTimeoutSec | Out-Null
    Assert-PendingCleared "windows" $offline_win.Process $offline_win.Log $OfflineRecoveryTimeoutSec
    Add-Lifecycle $key
  } catch { Dump-PacketLifecycle $key $_.Exception.Message; throw } finally {
    if ($offline_win) {
      if (-not $offline_win.Process.HasExited) {
        Wait-WindowsMarker $offline_win.Process $offline_win.Log "CHAT_EXIT_AFTER_PENDING_CLEAR" "Windows offline sender exit" 60 | Out-Null
        $deadline = (Get-Date).AddSeconds(30)
        while (-not $offline_win.Process.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
      }
      if (-not $offline_win.Process.HasExited) { Stop-WindowsRun $offline_win.Process }
      else { Stop-WindowsChat; Start-Sleep -Seconds 3 }
    }
  }

  # Final online proof after both recovery paths.
  Stop-WindowsChat
  Start-Sleep -Seconds 3
  $primary_log = Join-Path $out_dir "windows_primary_reconnect.log"
  Remove-Item $primary_log -ErrorAction SilentlyContinue
  Clear-Logcat $adb $Serial
  $primary = Start-WindowsRedirected $win_exe $repo_root $primary_args $primary_log
  Wait-WindowsMarker $primary $primary_log "AETHER_CLIENT_READY platform=windows" "Windows client ready after recovery" $ClientReadyTimeoutSec | Out-Null
  Wait-WindowsMarker $primary $primary_log "CHAT_SYNC_INITIAL_COMPLETE|CHAT_SYNC_RESUMED" "Windows sync ready after recovery" $SyncTimeoutSec | Out-Null
  Wait-WindowsMarker $primary $primary_log "P2P_OUTGOING_STATE peer=\S+.*state=writable" "Windows writable after recovery" $SyncTimeoutSec | Out-Null
  # Do not require a fresh Android writable marker: clearing logcat would hide an
  # already-writable outgoing that does not re-publish identical state.
  Start-Sleep -Seconds 2
  foreach ($direction in @("a2w", "w2a")) {
    $key = "${direction}_after_reconnect"
    try {
      if ($direction -eq "a2w") {
        Send-AndroidMessage $adb $Serial $key
        $watch = [System.Diagnostics.Stopwatch]::StartNew()
        Wait-WindowsMarker $primary $primary_log "CHAT_MESSAGE_VISIBLE platform=windows text_key=$key" "Windows saw $key" $MaxOnlineDeliverySec | Out-Null
        $watch.Stop(); Add-Latency "${direction}_after_reconnect" $key $watch.ElapsedMilliseconds
      } else {
        Send-WindowsInboxMessage $primary $primary_log $commit_inbox $key | Out-Null
        $watch = [System.Diagnostics.Stopwatch]::StartNew()
        Wait-Marker $adb $Serial "CHAT_MESSAGE_VISIBLE platform=android text_key=$key" "Android saw $key" $MaxOnlineDeliverySec | Out-Null
        $watch.Stop(); Add-Latency "${direction}_after_reconnect" $key $watch.ElapsedMilliseconds
      }
      if ($watch.Elapsed.TotalSeconds -gt $MaxOnlineDeliverySec) {
        throw "Delivery of $key took $($watch.ElapsedMilliseconds) ms (> ${MaxOnlineDeliverySec}s)"
      }
      Add-Lifecycle $key
    } catch { Dump-PacketLifecycle $key $_.Exception.Message; throw }
  }

  Save-Artifacts
  $all_logs = (Get-Logcat $adb $Serial) + "`n" + ((Get-ChildItem $out_dir -Filter "windows_*.log" | ForEach-Object { Get-WindowsLog $_.FullName }) -join "`n")
  Write-Utf8NoBom $generations_path (($all_logs -split "`n" | Where-Object { $_ -match "SYNC_TRANSPORT_WRITE.*generation=" }) -join "`r`n")
  Write-Host "`nDirectional delivery smoke PASSED"
  Write-Host "Artifacts: $out_dir"
} finally {
  Save-Artifacts
  Stop-WindowsChat
}
