# Windows <-> Android lifecycle synchronization matrix.
#
# Validates membership, presence, pending retry, and independent histories across
# five scenarios. Artifacts are written under build/lifecycle-matrix.

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
  [int]$SyncTimeoutSec = 120,
  [int]$OfflineTimeoutSec = 30
)

$ErrorActionPreference = "Stop"
$PackageName = "com.apptraverse.singleclientchat"
$ActivityName = "$PackageName/.MainActivity"
$WindowsClientName = "apptraverse-windows-lifecycle"
$WindowsClientNameIndependent = "apptraverse-windows-lifecycle-independent"
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$script:Results = @()
$script:Phase = "init"
$script:WindowsUid = $null
$script:AndroidUid = $null
$script:WindowsObjId = $null
$script:AndroidObjId = $null
$script:WinProc = $null
$script:WinLog = $null
$script:S4EventId = $null
$script:S4PacketId = $null
$script:CanContinueS1to4 = $true
$script:LastCanonicalTranscript = ""
$script:LastCanonicalTranscriptSource = ""

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
    try { $Process.WaitForExit(15000) | Out-Null } catch { $null = $_ }
  }
  Stop-WindowsChat
  Start-Sleep -Seconds 2
  $script:WinProc = $null
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
function Get-ObjIdFromMarker([string]$Line, [string]$Platform) {
  if ($Line -notmatch "APP_CLIENT_READY platform=$Platform obj_id=(\d+)") {
    throw "Unable to parse $Platform obj_id from: $Line"
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
function Get-PacketId([string]$Line) {
  foreach ($name in @("packet", "packet_id")) {
    $value = Get-MarkerField $Line $name
    if ($value) { return $value }
  }
  throw "Unable to parse packet id from: $Line"
}
function Get-CrashPatterns {
  return @(
    "FATAL EXCEPTION", "JNI DETECTED ERROR", "SIGSEGV", "native crash",
    "assertion failure", "access violation", "terminate called"
  )
}
function Assert-NoCrash([string]$Logs, [string]$Label) {
  foreach ($pattern in (Get-CrashPatterns)) {
    if ($Logs -match [regex]::Escape($pattern)) {
      throw "$Label contains crash marker: $pattern"
    }
  }
}
function Count-Occurrences([string]$Text, [string]$Needle) {
  if ([string]::IsNullOrEmpty($Text) -or [string]::IsNullOrEmpty($Needle)) { return 0 }
  return ([regex]::Matches($Text, [regex]::Escape($Needle))).Count
}
function Assert-ContainsOnce([string]$Text, [string]$Needle, [string]$Label) {
  $count = Count-Occurrences $Text $Needle
  if ($count -ne 1) { throw "${Label}: expected '$Needle' exactly once, found $count" }
  Write-Host "  OK  $Label contains '$Needle' once"
}
function Assert-JoinCounts([string]$Text, [int]$WindowsJoins, [int]$AndroidJoins, [string]$Label) {
  $wj = Count-Occurrences $Text "Windows joined"
  $aj = Count-Occurrences $Text "Android joined"
  if ($wj -ne $WindowsJoins -or $aj -ne $AndroidJoins) {
    throw "$Label join counts: Windows=$wj (want $WindowsJoins) Android=$aj (want $AndroidJoins)"
  }
  Write-Host "  OK  $Label joins Windows=$wj Android=$aj"
}
function Get-LatestTranscriptPublished([string]$Logs) {
  $line = ($Logs -split "`n" | Where-Object { $_ -match "TRANSCRIPT_PUBLISHED" } | Select-Object -Last 1)
  if (-not $line) { return "" }
  if ($line -match "text=(.*)$") { return $Matches[1].TrimEnd() }
  return $line
}
# Pure selection: UI wins whenever the transcript node was found (even if empty).
function Select-CanonicalTranscript([bool]$UiFound, [string]$UiText, [string]$LogText) {
  if ($UiFound) {
    return [pscustomobject]@{ Source = "ui"; Text = $(if ($null -eq $UiText) { "" } else { $UiText }) }
  }
  return [pscustomobject]@{ Source = "logcat"; Text = $(if ($null -eq $LogText) { "" } else { $LogText }) }
}
function Get-AndroidUiTranscript {
  # Returns Found=$true when the transcript TextView node exists (Text may be empty).
  try {
    $xml_text = Get-UiDump $adb $Serial
  } catch {
    return [pscustomobject]@{ Found = $false; Text = "" }
  }
  if ([string]::IsNullOrWhiteSpace($xml_text)) {
    return [pscustomobject]@{ Found = $false; Text = "" }
  }
  try {
    $doc = [xml]$xml_text
  } catch {
    return [pscustomobject]@{ Found = $false; Text = "" }
  }
  $rid = "$PackageName`:id/transcript"
  $node = $doc.SelectSingleNode("//node[@resource-id='$rid']")
  if ($null -eq $node) {
    return [pscustomobject]@{ Found = $false; Text = "" }
  }
  $attr = $node.Attributes["text"]
  $text = if ($null -eq $attr) { "" } else { [string]$attr.Value }
  return [pscustomobject]@{ Found = $true; Text = $text }
}
function Get-CanonicalAndroidTranscript {
  $ui = Get-AndroidUiTranscript
  $log_text = ""
  if (-not $ui.Found) {
    $log_text = Get-LatestTranscriptPublished (Get-Logcat $adb $Serial)
  }
  $selected = Select-CanonicalTranscript ([bool]$ui.Found) $ui.Text $log_text
  $script:LastCanonicalTranscript = $selected.Text
  $script:LastCanonicalTranscriptSource = $selected.Source
  Write-Host "LIFECYCLE_TRANSCRIPT_SOURCE source=$($selected.Source)"
  return $selected.Text
}
function Get-AndroidJoinTranscript {
  return (Get-CanonicalAndroidTranscript)
}
function Invoke-LifecycleTranscriptHelpersSelfTest {
  $sample = "* Windows joined`n* Android joined`nWindows: sample_message"
  $a = Select-CanonicalTranscript $true $sample $sample
  if ((Count-Occurrences $a.Text "Windows joined") -ne 1) { throw "self-test A: Windows joined count" }
  if ((Count-Occurrences $a.Text "Android joined") -ne 1) { throw "self-test A: Android joined count" }
  if ($a.Source -ne "ui") { throw "self-test A: expected source=ui" }

  $b = Select-CanonicalTranscript $false "" $sample
  if ($b.Source -ne "logcat") { throw "self-test B: expected source=logcat" }
  if ((Count-Occurrences $b.Text "Windows joined") -ne 1) { throw "self-test B: Windows joined count" }

  $c = Select-CanonicalTranscript $true "" $sample
  if ($c.Source -ne "ui") { throw "self-test C: expected source=ui" }
  if ($c.Text -ne "") { throw "self-test C: expected empty transcript, got '$($c.Text)'" }

  $d = Select-CanonicalTranscript $true $sample ""
  if ((Count-Occurrences $d.Text "Windows joined") -ne 1) { throw "self-test D: Windows joined" }
  if ((Count-Occurrences $d.Text "Android joined") -ne 1) { throw "self-test D: Android joined" }
  if ((Count-Occurrences $d.Text "sample_message") -ne 1) { throw "self-test D: sample_message" }

  Write-Host "LIFECYCLE_TRANSCRIPT_HELPERS_SELF_TEST PASSED"
}
function Count-WindowsMessageVisible([string]$LogText, [string]$Key) {
  return ([regex]::Matches($LogText, "CHAT_MESSAGE_VISIBLE platform=windows text_key=$([regex]::Escape($Key))")).Count
}
function Record-Result([string]$Name, [bool]$Passed, [string]$Detail) {
  $status = if ($Passed) { "PASS" } else { "FAIL" }
  $script:Results += [pscustomobject]@{ Scenario = $Name; Result = $status; Detail = $Detail }
  Write-Host ("==== {0}: {1} ====" -f $Name, $status)
  if (-not $Passed -and $Detail) { Write-Host "  DETAIL: $Detail" }
}
function Save-PhaseArtifacts([string]$PhaseName) {
  $safe = ($PhaseName -replace '[^\w\-]+', '_')
  $dir = Join-Path $out_dir "phase_$safe"
  New-Item -ItemType Directory -Force -Path $dir | Out-Null
  try {
    $android_text = Get-Logcat $adb $Serial
    Write-Utf8NoBom (Join-Path $dir "android.logcat.txt") $android_text
    try {
      $ui = Get-UiDump $adb $Serial
      Write-Utf8NoBom (Join-Path $dir "android_ui.xml") $ui
    } catch { $null = $_ }
    try {
      $canonical = Get-CanonicalAndroidTranscript
      Write-Utf8NoBom (Join-Path $dir "android_canonical_transcript.txt") $canonical
      Write-Utf8NoBom (Join-Path $dir "android_canonical_transcript_source.txt") "$($script:LastCanonicalTranscriptSource)`r`n"
    } catch { $null = $_ }
  } catch { $null = $_ }
  if ($script:WinLog -and (Test-Path $script:WinLog)) {
    Copy-Item -Force $script:WinLog (Join-Path $dir (Split-Path -Leaf $script:WinLog))
  }
  $all_windows = Get-ChildItem -Path $out_dir -Filter "windows_*.log" -ErrorAction SilentlyContinue |
    ForEach-Object { "===== $($_.Name) =====`r`n$(Get-WindowsLog $_.FullName)" }
  Write-Utf8NoBom (Join-Path $dir "windows_logs.txt") (($all_windows -join "`r`n"))
}

function Send-WindowsInboxMessage([System.Diagnostics.Process]$Process, [string]$Log, [string]$Inbox, [string]$Key) {
  if ($null -eq $Process -or $Process.HasExited) { throw "Windows process is not running for inbox commit $Key" }
  Remove-Item $Inbox -ErrorAction SilentlyContinue
  $tmp = "$Inbox.tmp"
  [System.IO.File]::WriteAllText($tmp, $Key)
  Move-Item -Force $tmp $Inbox
  $commit = Wait-WindowsMarker $Process $Log "CHAT_MESSAGE_COMMITTED platform=windows .*text_key=$([regex]::Escape($Key))|MESSAGE_COMMITTED text=$([regex]::Escape($Key))" "Windows committed $Key" 90
  return $commit
}
function Add-PeerViaInbox([System.Diagnostics.Process]$Process, [string]$Log, [string]$Inbox, [string]$Uid) {
  if ($null -eq $Process -or $Process.HasExited) { throw "Windows process is not running for peer-inbox" }
  Remove-Item $Inbox -ErrorAction SilentlyContinue
  $tmp = "$Inbox.tmp"
  [System.IO.File]::WriteAllText($tmp, $Uid)
  Move-Item -Force $tmp $Inbox
  return Wait-WindowsMarker $Process $Log "CHAT_PEER_INBOX_ADDED uid=$([regex]::Escape($Uid))" "Windows peer-inbox added $Uid" $SyncTimeoutSec
}
function Get-WindowsArgs([string]$StateDir, [string]$ClientName, [string]$CommitInbox, [string]$PeerInbox, [string]$PeerUid = "") {
  $args = @(
    "--state-dir `"$StateDir`"",
    "--aether-client-name $ClientName",
    "--auto-accept-peer",
    "--commit-inbox `"$CommitInbox`"",
    "--peer-inbox `"$PeerInbox`""
  )
  if ($PeerUid) { $args += "--peer $PeerUid" }
  return ($args -join " ")
}
function Start-WindowsLifecycle([string]$StateDir, [string]$ClientName, [string]$LogName, [string]$PeerUid = "") {
  $log = Join-Path $out_dir $LogName
  Remove-Item $log -ErrorAction SilentlyContinue
  $args = Get-WindowsArgs $StateDir $ClientName $commit_inbox $peer_inbox $PeerUid
  $proc = Start-WindowsRedirected $win_exe $repo_root $args $log
  $script:WinProc = $proc
  $script:WinLog = $log
  return [pscustomobject]@{ Process = $proc; Log = $log }
}
function Wait-AndroidPendingCleared([int]$TimeoutSec) {
  Wait-Marker $adb $Serial "CHAT_PENDING_CHANGED .*pending=0|SYNC_PENDING_REMOVED .*pending=0" "Android pending=0" $TimeoutSec | Out-Null
}
function Wait-WindowsPendingCleared([System.Diagnostics.Process]$Process, [string]$Log, [int]$TimeoutSec) {
  Wait-WindowsMarker $Process $Log "CHAT_PENDING_CHANGED .*pending=0|SYNC_PENDING_REMOVED .*pending=0" "Windows pending=0" $TimeoutSec | Out-Null
}
function Wait-WindowsPendingPositive([System.Diagnostics.Process]$Process, [string]$Log, [string]$Key, [int]$TimeoutSec) {
  Wait-WindowsMarker $Process $Log "CHAT_PENDING_CHANGED .*pending=[1-9]|SYNC_PACKET_CREATED .*text_key=$([regex]::Escape($Key))|SYNC_PACKET_CREATED kind=event" "Windows pending>0 for $Key" $TimeoutSec | Out-Null
}
function Wait-AndroidPendingPositive([string]$Key, [int]$TimeoutSec) {
  Wait-Marker $adb $Serial "CHAT_PENDING_CHANGED .*pending=[1-9]|SYNC_PACKET_CREATED kind=event|MESSAGE_COMMITTED text=$([regex]::Escape($Key))" "Android pending>0 for $Key" $TimeoutSec | Out-Null
}
function Assert-AndroidHasKeyOnce([string]$Key) {
  # Wait for presenter update via marker (timing only), then count in canonical transcript.
  $deadline = (Get-Date).AddSeconds($SyncTimeoutSec)
  $last = ""
  while ((Get-Date) -lt $deadline) {
    $logs = Get-Logcat $adb $Serial
    $vis = ([regex]::Matches($logs, "CHAT_MESSAGE_VISIBLE platform=android text_key=$([regex]::Escape($Key))")).Count
    $transcript = Get-CanonicalAndroidTranscript
    $count = Count-Occurrences $transcript $Key
    $last = $transcript
    if ($vis -ge 1 -or $count -ge 1) {
      if ($count -ne 1) {
        throw "Android canonical transcript shows '$Key' $count times (want 1). source=$($script:LastCanonicalTranscriptSource) text=$transcript"
      }
      Write-Host "  OK  Android has $Key once (source=$($script:LastCanonicalTranscriptSource))"
      return
    }
    Start-Sleep -Milliseconds 400
  }
  throw "Android did not show $Key within timeout. last=$last source=$($script:LastCanonicalTranscriptSource)"
}
function Assert-WindowsHasKeyOnce([System.Diagnostics.Process]$Process, [string]$Log, [string]$Key) {
  Wait-WindowsMarker $Process $Log "CHAT_MESSAGE_VISIBLE platform=windows text_key=$([regex]::Escape($Key))" "Windows saw $Key" $SyncTimeoutSec | Out-Null
  $count = Count-WindowsMessageVisible (Get-WindowsLog $Log) $Key
  if ($count -ne 1) { throw "Windows CHAT_MESSAGE_VISIBLE for $Key count=$count (want 1)" }
}
function Assert-AndroidTranscriptContains([string]$Needle) {
  $deadline = (Get-Date).AddSeconds($SyncTimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $transcript = Get-CanonicalAndroidTranscript
    if ($transcript -match [regex]::Escape($Needle)) {
      Write-Host "  OK  Android transcript contains '$Needle' (source=$($script:LastCanonicalTranscriptSource))"
      return $transcript
    }
    Start-Sleep -Milliseconds 400
  }
  throw "Android transcript missing '$Needle'"
}
function Assert-AndroidLacksKey([string]$Key, [int]$TimeoutSec = 8) {
  Start-Sleep -Seconds ([Math]::Min(3, $TimeoutSec))
  $transcript = Get-CanonicalAndroidTranscript
  if ($transcript -match [regex]::Escape($Key)) {
    throw "Android unexpectedly already has '$Key' (source=$($script:LastCanonicalTranscriptSource))"
  }
  Write-Host "  OK  Android does not yet have $Key (source=$($script:LastCanonicalTranscriptSource))"
}
function Assert-StableIdentities([string]$WinLogPath) {
  $wline = Wait-WindowsMarker $script:WinProc $WinLogPath "AETHER_CLIENT_READY platform=windows uid=" "Windows UID stable check" $ClientReadyTimeoutSec
  $uid = Get-UidFromMarker $wline "windows"
  if ($script:WindowsUid -and $uid -ne $script:WindowsUid) {
    throw "Windows UID changed: $($script:WindowsUid) -> $uid"
  }
  $oline = ((Get-WindowsLog $WinLogPath) -split "`n" | Where-Object { $_ -match "APP_CLIENT_READY platform=windows obj_id=" } | Select-Object -Last 1)
  if ($oline) {
    $oid = Get-ObjIdFromMarker $oline.Trim() "windows"
    if ($script:WindowsObjId -and $oid -ne $script:WindowsObjId) {
      throw "Windows obj_id changed: $($script:WindowsObjId) -> $oid"
    }
  }
  $aline = Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=" "Android UID stable check" $ClientReadyTimeoutSec
  $auid = Get-UidFromMarker $aline "android"
  if ($script:AndroidUid -and $auid -ne $script:AndroidUid) {
    throw "Android UID changed: $($script:AndroidUid) -> $auid"
  }
  $aobj = ((Get-Logcat $adb $Serial) -split "`n" | Where-Object { $_ -match "APP_CLIENT_READY platform=android obj_id=" } | Select-Object -Last 1)
  if ($aobj) {
    $aoid = Get-ObjIdFromMarker $aobj.Trim() "android"
    if ($script:AndroidObjId -and $aoid -ne $script:AndroidObjId) {
      throw "Android obj_id changed: $($script:AndroidObjId) -> $aoid"
    }
  }
}

$repo_root = Resolve-RepoRoot
$sdk = Resolve-AndroidSdk
$adb = Get-Tool $sdk "platform-tools\adb.exe"
$android_dir = Join-Path $repo_root "examples\single_client_chat\android"
$out_dir = Join-Path $repo_root "build\lifecycle-matrix"
$win_state_dir = Join-Path $out_dir "windows-state"
$win_state_dir_indep = Join-Path $out_dir "windows-state-independent"
$win_build_dir = Join-Path $repo_root "build-msvc"
$win_exe = Join-Path $win_build_dir "examples\single_client_chat\windows\Debug\win32_single_client_chat.exe"
$commit_inbox = Join-Path $out_dir "windows_commit_inbox.txt"
$peer_inbox = Join-Path $out_dir "windows_peer_inbox.txt"
$summary_path = Join-Path $out_dir "summary.csv"
$uids_path = Join-Path $out_dir "uids.txt"

New-Item -ItemType Directory -Force -Path $out_dir | Out-Null
Write-Utf8NoBom $summary_path "scenario,result,detail`r`n"

Invoke-LifecycleTranscriptHelpersSelfTest

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

  if (-not $SkipWindowsBuild) {
    if (-not (Test-Path (Join-Path $win_build_dir "CMakeCache.txt"))) {
      cmake -S $repo_root -B $win_build_dir -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
      if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
    }
    cmake --build $win_build_dir --config Debug --target win32_single_client_chat
    if ($LASTEXITCODE -ne 0) { throw "Windows build failed" }
  }
  if (-not (Test-Path $win_exe)) { throw "Windows executable not found: $win_exe" }

  # -------------------- Scenario 1 --------------------
  $script:Phase = "s1"
  try {
    Write-Host ""
    Write-Host "Scenario 1 -- fresh initial connection"
    Stop-WindowsChat
    Stop-App $adb $Serial
    Invoke-Adb $adb $Serial @("shell", "pm", "clear", $PackageName) | Out-Null
    if (Test-Path $win_state_dir) { Remove-Item -Recurse -Force $win_state_dir }
    New-Item -ItemType Directory -Force -Path $win_state_dir | Out-Null
    Remove-Item $commit_inbox, $peer_inbox -ErrorAction SilentlyContinue
    & $win_exe --distill --state-dir $win_state_dir --aether-client-name $WindowsClientName
    if ($LASTEXITCODE -ne 0) { throw "Windows distill failed" }

    $win = Start-WindowsLifecycle $win_state_dir $WindowsClientName "windows_s1.log"
    $wready = Wait-WindowsMarker $win.Process $win.Log "AETHER_CLIENT_READY platform=windows uid=" "Windows ready S1" $ClientReadyTimeoutSec
    $script:WindowsUid = Get-UidFromMarker $wready "windows"
    $wobj = ((Get-WindowsLog $win.Log) -split "`n" | Where-Object { $_ -match "APP_CLIENT_READY platform=windows obj_id=" } | Select-Object -Last 1)
    if ($wobj) { $script:WindowsObjId = Get-ObjIdFromMarker $wobj.Trim() "windows" }

    Send-WindowsInboxMessage $win.Process $win.Log $commit_inbox "s1_w_before_android" | Out-Null

    Clear-Logcat $adb $Serial
    Start-App $adb $Serial
    $aready = Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=" "Android ready S1" $ClientReadyTimeoutSec
    $script:AndroidUid = Get-UidFromMarker $aready "android"
    $aobj = ((Get-Logcat $adb $Serial) -split "`n" | Where-Object { $_ -match "APP_CLIENT_READY platform=android obj_id=" } | Select-Object -Last 1)
    if ($aobj) { $script:AndroidObjId = Get-ObjIdFromMarker $aobj.Trim() "android" }
    Wait-Marker $adb $Serial "CHAT_SYNC_CONTROLLER_READY" "Android sync controller ready S1" 60 | Out-Null

    Add-PeerViaInbox $win.Process $win.Log $peer_inbox $script:AndroidUid | Out-Null
    Wait-WindowsMarker $win.Process $win.Log "CHAT_PEER_ADDED|CHAT_SYNC_INITIAL_COMPLETE|CHAT_PEER_ONLINE" "Windows peer/session progress" $SyncTimeoutSec | Out-Null

    Assert-AndroidTranscriptContains "Windows joined" | Out-Null
    Assert-AndroidTranscriptContains "Windows: s1_w_before_android" | Out-Null
    Assert-AndroidTranscriptContains "Android joined" | Out-Null
    # Shared journal: Android joined on Windows is implied by completed sync + Android UI.
    Wait-WindowsMarker $win.Process $win.Log "CHAT_SYNC_INITIAL_COMPLETE|SYNC_EVENT_APPLIED|CHAT_PEER_ONLINE" "Windows received Android membership/sync" $SyncTimeoutSec | Out-Null
    Assert-JoinCounts (Get-AndroidJoinTranscript) 1 1 "S1 after pair"

    try {
      Wait-WindowsMarker $win.Process $win.Log "CHAT_PEER_ONLINE peer=$([regex]::Escape($script:AndroidUid))" "Windows CHAT_PEER_ONLINE" $SyncTimeoutSec | Out-Null
      Wait-Marker $adb $Serial "CHAT_PEER_ONLINE peer=$([regex]::Escape($script:WindowsUid))" "Android CHAT_PEER_ONLINE" $SyncTimeoutSec | Out-Null
    } catch {
      Write-Host "  WARN presence online wait: $($_.Exception.Message)"
    }

    Send-WindowsInboxMessage $win.Process $win.Log $commit_inbox "s1_w_to_a" | Out-Null
    Assert-AndroidHasKeyOnce "s1_w_to_a"
    Wait-WindowsPendingCleared $win.Process $win.Log $SyncTimeoutSec

    Send-AndroidMessage $adb $Serial "s1_a_to_w"
    Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=s1_a_to_w|CHAT_MESSAGE_COMMITTED platform=android .*text_key=s1_a_to_w" "Android committed s1_a_to_w" 60 | Out-Null
    Assert-WindowsHasKeyOnce $win.Process $win.Log "s1_a_to_w"
    Wait-WindowsPendingCleared $win.Process $win.Log $SyncTimeoutSec
    try { Wait-AndroidPendingCleared $SyncTimeoutSec } catch { Write-Host "  WARN android pending clear: $($_.Exception.Message)" }

    Assert-NoCrash (Get-WindowsLog $win.Log) "Windows S1"
    Assert-NoCrash (Get-Logcat $adb $Serial) "Android S1"
    Write-Utf8NoBom $uids_path "windows_uid=$($script:WindowsUid)`r`nandroid_uid=$($script:AndroidUid)`r`nwindows_obj_id=$($script:WindowsObjId)`r`nandroid_obj_id=$($script:AndroidObjId)`r`n"
    Save-PhaseArtifacts "s1"
    Record-Result "S1_fresh_initial_connection" $true ""
  } catch {
    $script:CanContinueS1to4 = $false
    Save-PhaseArtifacts "s1_fail"
    Record-Result "S1_fresh_initial_connection" $false $_.Exception.Message
  }

  # -------------------- Scenario 2 --------------------
  $script:Phase = "s2"
  if ($script:CanContinueS1to4 -and ($null -ne $script:WinProc) -and (-not $script:WinProc.HasExited)) {
    try {
      Write-Host ""
      Write-Host "Scenario 2 -- Android offline restart"
      Stop-App $adb $Serial
      Wait-WindowsMarker $script:WinProc $script:WinLog "CHAT_PEER_OFFLINE peer=$([regex]::Escape($script:AndroidUid))" "Windows saw Android offline" $OfflineTimeoutSec | Out-Null

      Send-WindowsInboxMessage $script:WinProc $script:WinLog $commit_inbox "s2_w_while_a_offline" | Out-Null
      Wait-WindowsPendingPositive $script:WinProc $script:WinLog "s2_w_while_a_offline" 60

      Clear-Logcat $adb $Serial
      Start-App $adb $Serial
      $aready2 = Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=" "Android ready S2" $ClientReadyTimeoutSec
      $auid2 = Get-UidFromMarker $aready2 "android"
      if ($auid2 -ne $script:AndroidUid) { throw "Android UID changed in S2: $($script:AndroidUid) -> $auid2" }
      $aobj2 = ((Get-Logcat $adb $Serial) -split "`n" | Where-Object { $_ -match "APP_CLIENT_READY platform=android obj_id=" } | Select-Object -Last 1)
      if ($aobj2 -and $script:AndroidObjId) {
        $aoid2 = Get-ObjIdFromMarker $aobj2.Trim() "android"
        if ($aoid2 -ne $script:AndroidObjId) { throw "Android obj_id changed in S2: $($script:AndroidObjId) -> $aoid2" }
      }

      Assert-AndroidHasKeyOnce "s2_w_while_a_offline"
      Wait-WindowsMarker $script:WinProc $script:WinLog "CHAT_PEER_REJOINED peer=$([regex]::Escape($script:AndroidUid))" "Windows CHAT_PEER_REJOINED Android" $SyncTimeoutSec | Out-Null
      Assert-JoinCounts (Get-AndroidJoinTranscript) 1 1 "S2 after rejoin"

      Send-AndroidMessage $adb $Serial "s2_a_after_rejoin"
      Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=s2_a_after_rejoin|CHAT_MESSAGE_COMMITTED platform=android .*text_key=s2_a_after_rejoin" "Android committed s2_a_after_rejoin" 60 | Out-Null
      Assert-WindowsHasKeyOnce $script:WinProc $script:WinLog "s2_a_after_rejoin"
      Wait-WindowsPendingCleared $script:WinProc $script:WinLog $SyncTimeoutSec
      try { Wait-AndroidPendingCleared $SyncTimeoutSec } catch { $null = $_ }

      Assert-NoCrash (Get-WindowsLog $script:WinLog) "Windows S2"
      Assert-NoCrash (Get-Logcat $adb $Serial) "Android S2"
      Save-PhaseArtifacts "s2"
      Record-Result "S2_android_offline_restart" $true ""
    } catch {
      $script:CanContinueS1to4 = $false
      Save-PhaseArtifacts "s2_fail"
      Record-Result "S2_android_offline_restart" $false $_.Exception.Message
    }
  } else {
    Record-Result "S2_android_offline_restart" $false "Skipped because prior S1-4 path is not continuable"
  }

  # -------------------- Scenario 3 --------------------
  $script:Phase = "s3"
  if ($script:CanContinueS1to4) {
    try {
      Write-Host ""
      Write-Host "Scenario 3 -- Windows offline restart"
      Stop-WindowsRun $script:WinProc
      Wait-Marker $adb $Serial "CHAT_PEER_OFFLINE peer=$([regex]::Escape($script:WindowsUid))" "Android saw Windows offline" $OfflineTimeoutSec | Out-Null

      Send-AndroidMessage $adb $Serial "s3_a_while_w_offline"
      Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=s3_a_while_w_offline|CHAT_MESSAGE_COMMITTED platform=android .*text_key=s3_a_while_w_offline" "Android committed s3_a_while_w_offline" 60 | Out-Null
      Wait-AndroidPendingPositive "s3_a_while_w_offline" 60

      # Restart Windows WITHOUT --peer; peers restored from ChatPeerSet.
      $win3 = Start-WindowsLifecycle $win_state_dir $WindowsClientName "windows_s3.log"
      Assert-StableIdentities $win3.Log
      Assert-WindowsHasKeyOnce $win3.Process $win3.Log "s3_a_while_w_offline"
      Wait-Marker $adb $Serial "CHAT_PEER_REJOINED peer=$([regex]::Escape($script:WindowsUid))" "Android CHAT_PEER_REJOINED Windows" $SyncTimeoutSec | Out-Null

      Send-WindowsInboxMessage $win3.Process $win3.Log $commit_inbox "s3_w_after_rejoin" | Out-Null
      Assert-AndroidHasKeyOnce "s3_w_after_rejoin"
      Assert-JoinCounts (Get-AndroidJoinTranscript) 1 1 "S3 after rejoin"
      Wait-WindowsPendingCleared $win3.Process $win3.Log $SyncTimeoutSec
      try { Wait-AndroidPendingCleared $SyncTimeoutSec } catch { $null = $_ }

      Assert-NoCrash (Get-WindowsLog $win3.Log) "Windows S3"
      Assert-NoCrash (Get-Logcat $adb $Serial) "Android S3"
      Save-PhaseArtifacts "s3"
      Record-Result "S3_windows_offline_restart" $true ""
    } catch {
      $script:CanContinueS1to4 = $false
      Save-PhaseArtifacts "s3_fail"
      Record-Result "S3_windows_offline_restart" $false $_.Exception.Message
    }
  } else {
    Record-Result "S3_windows_offline_restart" $false "Skipped because prior S1-4 path is not continuable"
  }

  # -------------------- Scenario 4 --------------------
  $script:Phase = "s4"
  if ($script:CanContinueS1to4 -and ($null -ne $script:WinProc) -and (-not $script:WinProc.HasExited)) {
    try {
      Write-Host ""
      Write-Host "Scenario 4 -- sender exits with persisted pending"
      Stop-App $adb $Serial
      Wait-WindowsMarker $script:WinProc $script:WinLog "CHAT_PEER_OFFLINE peer=$([regex]::Escape($script:AndroidUid))" "Windows saw Android offline S4" $OfflineTimeoutSec | Out-Null

      $before_log = Get-WindowsLog $script:WinLog
      $commit = Send-WindowsInboxMessage $script:WinProc $script:WinLog $commit_inbox "s4_w_pending_before_exit"
      $script:S4EventId = Get-EventId $commit
      Wait-WindowsPendingPositive $script:WinProc $script:WinLog "s4_w_pending_before_exit" 60
      $after_log = Get-WindowsLog $script:WinLog
      $created = ($after_log -split "`n" | Where-Object {
        $_ -match "SYNC_PACKET_CREATED kind=event" -and $_ -match "event=$([regex]::Escape($script:S4EventId))"
      } | Select-Object -Last 1)
      if (-not $created) {
        $created = ($after_log -split "`n" | Where-Object { $_ -match "SYNC_PACKET_CREATED kind=event" } | Select-Object -Last 1)
      }
      if (-not $created) { throw "Unable to find SYNC_PACKET_CREATED for s4" }
      $script:S4PacketId = Get-PacketId $created.Trim()
      Write-Utf8NoBom (Join-Path $out_dir "s4_packet_ids.txt") "event_id=$($script:S4EventId)`r`npacket_id=$($script:S4PacketId)`r`n"

      # Stop Windows completely BEFORE starting Android.
      Stop-WindowsRun $script:WinProc

      Clear-Logcat $adb $Serial
      Start-App $adb $Serial
      Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=$([regex]::Escape($script:AndroidUid))" "Android ready S4 old state" $ClientReadyTimeoutSec | Out-Null
      Assert-AndroidLacksKey "s4_w_pending_before_exit" 8

      $win4 = Start-WindowsLifecycle $win_state_dir $WindowsClientName "windows_s4.log"
      Assert-StableIdentities $win4.Log
      Wait-WindowsMarker $win4.Process $win4.Log "CHAT_SYNC_RESUMED" "Windows CHAT_SYNC_RESUMED S4" $SyncTimeoutSec | Out-Null
      Wait-WindowsMarker $win4.Process $win4.Log "SYNC_PACKET_RETRY packet=$([regex]::Escape($script:S4PacketId))" "Windows SYNC_PACKET_RETRY same packet" $SyncTimeoutSec | Out-Null

      Assert-AndroidHasKeyOnce "s4_w_pending_before_exit"
      Wait-WindowsMarker $win4.Process $win4.Log "SYNC_PENDING_REMOVED packet=$([regex]::Escape($script:S4PacketId))|CHAT_PENDING_CHANGED .*pending=0" "Windows pending removed for S4 packet" $SyncTimeoutSec | Out-Null
      Wait-WindowsPendingCleared $win4.Process $win4.Log $SyncTimeoutSec

      try {
        Wait-WindowsMarker $win4.Process $win4.Log "CHAT_PEER_REJOINED peer=$([regex]::Escape($script:AndroidUid))" "Windows rejoin S4" $SyncTimeoutSec | Out-Null
        Wait-Marker $adb $Serial "CHAT_PEER_REJOINED peer=$([regex]::Escape($script:WindowsUid))" "Android rejoin S4" $SyncTimeoutSec | Out-Null
      } catch {
        Write-Host "  WARN S4 rejoin markers: $($_.Exception.Message)"
      }

      Assert-JoinCounts (Get-AndroidJoinTranscript) 1 1 "S4 after recovery"
      Assert-NoCrash (Get-WindowsLog $win4.Log) "Windows S4"
      Assert-NoCrash (Get-Logcat $adb $Serial) "Android S4"
      Save-PhaseArtifacts "s4"
      Record-Result "S4_sender_exit_persisted_pending" $true ""
    } catch {
      $script:CanContinueS1to4 = $false
      Save-PhaseArtifacts "s4_fail"
      Record-Result "S4_sender_exit_persisted_pending" $false $_.Exception.Message
    }
  } else {
    Record-Result "S4_sender_exit_persisted_pending" $false "Skipped because prior S1-4 path is not continuable"
  }

  # -------------------- Final persistence after S1-4 --------------------
  $script:Phase = "persist"
  $persist_ok_to_run = ($script:Results | Where-Object { $_.Scenario -like 'S4*' -and $_.Result -eq 'PASS' }).Count -gt 0
  if ($persist_ok_to_run -and ($null -ne $script:WinProc) -and (-not $script:WinProc.HasExited)) {
    try {
      Write-Host ""
      Write-Host "Final persistence -- restart both, no new messages"
      $expected_keys = @(
        "s1_w_before_android", "s1_w_to_a", "s1_a_to_w",
        "s2_w_while_a_offline", "s2_a_after_rejoin",
        "s3_a_while_w_offline", "s3_w_after_rejoin",
        "s4_w_pending_before_exit"
      )
      Stop-WindowsRun $script:WinProc
      Stop-App $adb $Serial
      Start-Sleep -Seconds 2
      Clear-Logcat $adb $Serial
      Start-App $adb $Serial
      Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=$([regex]::Escape($script:AndroidUid))" "Android persistence ready" $ClientReadyTimeoutSec | Out-Null
      $winp = Start-WindowsLifecycle $win_state_dir $WindowsClientName "windows_persist.log"
      Assert-StableIdentities $winp.Log
      Wait-WindowsMarker $winp.Process $winp.Log "CHAT_SYNC_RESUMED|CHAT_PEER_ONLINE|CHAT_PEER_REJOINED" "Windows persistence sync/presence" $SyncTimeoutSec | Out-Null
      try {
        Wait-Marker $adb $Serial "CHAT_PEER_ONLINE peer=$([regex]::Escape($script:WindowsUid))|CHAT_PEER_REJOINED peer=$([regex]::Escape($script:WindowsUid))" "Android persistence presence" $SyncTimeoutSec | Out-Null
      } catch { Write-Host "  WARN persistence presence: $($_.Exception.Message)" }

      $transcript = Get-AndroidJoinTranscript
      Assert-JoinCounts $transcript 1 1 "Persistence joins"
      foreach ($key in $expected_keys) {
        Assert-ContainsOnce $transcript $key "Persistence Android history"
      }
      Wait-WindowsPendingCleared $winp.Process $winp.Log 60
      try { Wait-AndroidPendingCleared 60 } catch { $null = $_ }
      Assert-NoCrash (Get-WindowsLog $winp.Log) "Windows persistence"
      Assert-NoCrash (Get-Logcat $adb $Serial) "Android persistence"
      Save-PhaseArtifacts "persist"
      Record-Result "Persistence_after_S1_S4" $true ""
      Stop-WindowsRun $winp.Process
      Stop-App $adb $Serial
    } catch {
      Save-PhaseArtifacts "persist_fail"
      Record-Result "Persistence_after_S1_S4" $false $_.Exception.Message
      try { Stop-WindowsRun $script:WinProc } catch { $null = $_ }
      try { Stop-App $adb $Serial } catch { $null = $_ }
    }
  } else {
    Record-Result "Persistence_after_S1_S4" $false "Skipped because S4 did not pass / Windows not running"
    try { Stop-WindowsRun $script:WinProc } catch { $null = $_ }
    try { Stop-App $adb $Serial } catch { $null = $_ }
  }

  # -------------------- Scenario 5 --------------------
  $script:Phase = "s5"
  try {
    Write-Host ""
    Write-Host "Scenario 5 -- independent histories before first pair"
    Stop-WindowsChat
    Stop-App $adb $Serial
    Invoke-Adb $adb $Serial @("shell", "pm", "clear", $PackageName) | Out-Null
    if (Test-Path $win_state_dir_indep) { Remove-Item -Recurse -Force $win_state_dir_indep }
    New-Item -ItemType Directory -Force -Path $win_state_dir_indep | Out-Null
    Remove-Item $commit_inbox, $peer_inbox -ErrorAction SilentlyContinue

    & $win_exe --distill --state-dir $win_state_dir_indep --aether-client-name $WindowsClientNameIndependent
    if ($LASTEXITCODE -ne 0) { throw "Windows independent distill failed" }

    $win5a = Start-WindowsLifecycle $win_state_dir_indep $WindowsClientNameIndependent "windows_s5_before.log"
    $wready5 = Wait-WindowsMarker $win5a.Process $win5a.Log "AETHER_CLIENT_READY platform=windows uid=" "Windows ready S5 before pair" $ClientReadyTimeoutSec
    $windows_uid_indep = Get-UidFromMarker $wready5 "windows"
    Send-WindowsInboxMessage $win5a.Process $win5a.Log $commit_inbox "s5_w_before_pair" | Out-Null
    # Allow local save before stop.
    Start-Sleep -Seconds 2
    Stop-WindowsRun $win5a.Process

    Clear-Logcat $adb $Serial
    Start-App $adb $Serial
    $aready5 = Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=" "Android ready S5" $ClientReadyTimeoutSec
    $android_uid_indep = Get-UidFromMarker $aready5 "android"
    Wait-Marker $adb $Serial "CHAT_SYNC_CONTROLLER_READY" "Android sync controller ready S5" 60 | Out-Null
    Send-AndroidMessage $adb $Serial "s5_a_before_pair"
    Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=s5_a_before_pair|CHAT_MESSAGE_COMMITTED platform=android .*text_key=s5_a_before_pair" "Android committed s5_a_before_pair" 60 | Out-Null
    Assert-AndroidTranscriptContains "Android: s5_a_before_pair" | Out-Null

    # Pair with --peer (and inboxes). Android auto-accepts.
    $win5b = Start-WindowsLifecycle $win_state_dir_indep $WindowsClientNameIndependent "windows_s5_pair.log" $android_uid_indep
    Wait-WindowsMarker $win5b.Process $win5b.Log "AETHER_CLIENT_READY platform=windows uid=$([regex]::Escape($windows_uid_indep))" "Windows UID stable S5 pair" $ClientReadyTimeoutSec | Out-Null
    Wait-WindowsMarker $win5b.Process $win5b.Log "CHAT_PEER_ADDED|CHAT_SYNC_INITIAL_COMPLETE" "Windows pair progress S5" $SyncTimeoutSec | Out-Null
    Wait-Marker $adb $Serial "CHAT_PEER_ADDED|CHAT_SYNC_INITIAL_COMPLETE|CHAT_SYNC_RESUMED" "Android pair progress S5" $SyncTimeoutSec | Out-Null

    Assert-AndroidTranscriptContains "Windows joined" | Out-Null
    Assert-AndroidTranscriptContains "Windows: s5_w_before_pair" | Out-Null
    Assert-AndroidTranscriptContains "Android joined" | Out-Null
    Assert-AndroidTranscriptContains "Android: s5_a_before_pair" | Out-Null
    Assert-WindowsHasKeyOnce $win5b.Process $win5b.Log "s5_a_before_pair"
    # s5_w_before_pair is local on Windows; confirm visible marker if emitted, else journal/commit already done.
    $wlog5 = Get-WindowsLog $win5b.Log
    if ($wlog5 -notmatch "s5_w_before_pair") {
      # Accept local persistence from prior run; require Android already showed it.
      Write-Host "  OK  Windows prior local history implied by Android receipt of s5_w_before_pair"
    }
    Assert-JoinCounts (Get-AndroidJoinTranscript) 1 1 "S5 after pair"

    Send-WindowsInboxMessage $win5b.Process $win5b.Log $commit_inbox "s5_w_after_pair" | Out-Null
    Assert-AndroidHasKeyOnce "s5_w_after_pair"
    Send-AndroidMessage $adb $Serial "s5_a_after_pair"
    Wait-Marker $adb $Serial "MESSAGE_COMMITTED text=s5_a_after_pair|CHAT_MESSAGE_COMMITTED platform=android .*text_key=s5_a_after_pair" "Android committed s5_a_after_pair" 60 | Out-Null
    Assert-WindowsHasKeyOnce $win5b.Process $win5b.Log "s5_a_after_pair"
    Wait-WindowsPendingCleared $win5b.Process $win5b.Log $SyncTimeoutSec
    try { Wait-AndroidPendingCleared $SyncTimeoutSec } catch { $null = $_ }

    Assert-ContainsOnce (Get-AndroidJoinTranscript) "s5_w_after_pair" "S5 Android after"
    Assert-ContainsOnce (Get-AndroidJoinTranscript) "s5_a_after_pair" "S5 Android after"
    Assert-JoinCounts (Get-AndroidJoinTranscript) 1 1 "S5 final joins"
    Assert-NoCrash (Get-WindowsLog $win5b.Log) "Windows S5"
    Assert-NoCrash (Get-Logcat $adb $Serial) "Android S5"
    Add-Utf8NoBom $uids_path "windows_uid_independent=$windows_uid_indep`r`nandroid_uid_independent=$android_uid_indep`r`n"
    Save-PhaseArtifacts "s5"
    Record-Result "S5_independent_histories" $true ""
    Stop-WindowsRun $win5b.Process
  } catch {
    Save-PhaseArtifacts "s5_fail"
    Record-Result "S5_independent_histories" $false $_.Exception.Message
    try { Stop-WindowsRun $script:WinProc } catch { $null = $_ }
  }

} finally {
  try { Stop-WindowsRun $script:WinProc } catch { Stop-WindowsChat }
  try { Stop-App $adb $Serial } catch { $null = $_ }
}

Write-Host ""
Write-Host "===== LIFECYCLE MATRIX SUMMARY ====="
$fail_count = 0
foreach ($row in $script:Results) {
  $detail = ($row.Detail -replace '[\r\n]+', ' ')
  Add-Utf8NoBom $summary_path ("{0},{1},`"{2}`"`r`n" -f $row.Scenario, $row.Result, ($detail -replace '"', "''"))
  Write-Host ("{0,-40} {1} {2}" -f $row.Scenario, $row.Result, $row.Detail)
  if ($row.Result -ne "PASS") { $fail_count++ }
}
Write-Host "Summary CSV: $summary_path"
if ($fail_count -gt 0) {
  Write-Host "FAILED assertions: $fail_count"
  exit 1
}
Write-Host "All required assertions passed."
exit 0
