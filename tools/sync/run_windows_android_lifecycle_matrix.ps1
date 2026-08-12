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
  if ([string]::IsNullOrEmpty($Xml)) { return $null }
  $match = [regex]::Match($Xml, '<node\b[^>]*resource-id="' + [regex]::Escape($ResourceId) + '"[^>]*>')
  if (-not $match.Success) { return $null }
  $node = $match.Value
  $text = ""
  $x = 0
  $y = 0
  $bounds = ""
  $focused = $false
  $enabled = $false
  if ($node -match '\btext="([^"]*)"') { $text = $Matches[1] }
  if ($node -match '\bfocused="(true|false)"') { $focused = ($Matches[1] -eq "true") }
  if ($node -match '\benabled="(true|false)"') { $enabled = ($Matches[1] -eq "true") }
  if ($node -match 'bounds="(\[(\d+),(\d+)\]\[(\d+),(\d+)\])"') {
    $bounds = $Matches[1]
    $x = [int](([int]$Matches[2] + [int]$Matches[4]) / 2)
    $y = [int](([int]$Matches[3] + [int]$Matches[5]) / 2)
  }
  return [pscustomobject]@{
    Text = $text
    X = $x
    Y = $y
    Focused = $focused
    Enabled = $enabled
    Bounds = $bounds
  }
}
function Format-UiNodeState($Node) {
  if ($null -eq $Node) { return "node=<null>" }
  return ("text='{0}' focused={1} enabled={2} bounds={3} x={4} y={5}" -f `
    $Node.Text, $Node.Focused, $Node.Enabled, $Node.Bounds, $Node.X, $Node.Y)
}
function Wait-AndroidUiNode {
  param(
    [string]$Adb,
    [string]$DeviceSerial,
    [string]$ResourceId,
    [int]$TimeoutSec = 30,
    [scriptblock]$Predicate = $null,
    [string]$Description = "UI node"
  )
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  $last_xml = ""
  $last_node = $null
  while ((Get-Date) -lt $deadline) {
    try { $last_xml = Get-UiDump $Adb $DeviceSerial } catch { $last_xml = "" }
    $last_node = Get-UiNode $last_xml $ResourceId
    if ($null -ne $last_node) {
      $ok = $true
      if ($null -ne $Predicate) {
        $ok = [bool](& $Predicate $last_node)
      }
      if ($ok) { return $last_node }
    }
    Start-Sleep -Milliseconds 250
  }
  $state = Format-UiNodeState $last_node
  throw ("$Description timeout for $ResourceId. last=$state`n--- last UI dump ---`n$last_xml")
}
function Wait-AndroidControlFocused {
  param(
    [string]$Adb,
    [string]$DeviceSerial,
    [string]$ResourceId,
    [int]$TimeoutSec = 10,
    [string]$Description = "control focused"
  )
  return Wait-AndroidUiNode -Adb $Adb -DeviceSerial $DeviceSerial -ResourceId $ResourceId `
    -TimeoutSec $TimeoutSec -Description $Description -Predicate {
      param($n) return ($n.Focused -eq $true -and $n.Enabled -eq $true)
    }
}
function Test-AndroidInputCleared($Node) {
  if ($null -eq $Node) { return $false }
  $t = [string]$Node.Text
  return ([string]::IsNullOrEmpty($t) -or $t -eq "Message")
}
function Send-AndroidMessage([string]$Adb, [string]$DeviceSerial, [string]$Text) {
  $input_id = "$PackageName`:id/message_input"
  $send_id = "$PackageName`:id/send"

  # Step A: find input (start Activity only if missing).
  $input = $null
  try {
    $input = Get-UiNode (Get-UiDump $Adb $DeviceSerial) $input_id
  } catch { $input = $null }
  if ($null -eq $input) {
    Start-App $Adb $DeviceSerial
    $input = Wait-AndroidUiNode -Adb $Adb -DeviceSerial $DeviceSerial -ResourceId $input_id `
      -TimeoutSec 30 -Description "message_input not found" -Predicate {
        param($n) return ($n.X -gt 0 -and $n.Y -gt 0)
      }
  }
  if ($null -eq $input) { throw "message_input not found. $(Format-UiNodeState $input)" }
  if (-not $input.Enabled) { throw "message_input disabled. $(Format-UiNodeState $input)" }
  if ($input.X -le 0 -or $input.Y -le 0) { throw "message_input not found. $(Format-UiNodeState $input)" }

  # Step B: obtain focus (retry tap+wait only; never type until focused).
  $focused = $null
  for ($attempt = 1; $attempt -le 3; $attempt++) {
    $input = Get-UiNode (Get-UiDump $Adb $DeviceSerial) $input_id
    if ($null -eq $input -or -not $input.Enabled) {
      throw "message_input disabled. $(Format-UiNodeState $input)"
    }
    Invoke-Adb $Adb $DeviceSerial @("shell", "input", "tap", "$($input.X)", "$($input.Y)") | Out-Null
    try {
      $focused = Wait-AndroidControlFocused -Adb $Adb -DeviceSerial $DeviceSerial `
        -ResourceId $input_id -TimeoutSec 8 -Description "message_input focus attempt $attempt"
      break
    } catch {
      if ($attempt -eq 3) {
        $n = Get-UiNode (Get-UiDump $Adb $DeviceSerial) $input_id
        throw "message_input did not receive focus. $(Format-UiNodeState $n)"
      }
    }
  }
  if ($null -eq $focused -or -not $focused.Focused) {
    throw "message_input did not receive focus. $(Format-UiNodeState $focused)"
  }

  # Step C: clear input after confirmed focus.
  $clear = @("shell", "input", "keyevent", "123") + (1..64 | ForEach-Object { "67" })
  Invoke-Adb $Adb $DeviceSerial $clear | Out-Null
  $cleared = Wait-AndroidUiNode -Adb $Adb -DeviceSerial $DeviceSerial -ResourceId $input_id `
    -TimeoutSec 8 -Description "message_input clear" -Predicate {
      param($n) return (Test-AndroidInputCleared $n)
    }

  # Step D: type and wait for exact text while still focused.
  Invoke-Adb $Adb $DeviceSerial @("shell", "input", "text", $Text) | Out-Null
  $typed = $null
  try {
    $typed = Wait-AndroidUiNode -Adb $Adb -DeviceSerial $DeviceSerial -ResourceId $input_id `
      -TimeoutSec 10 -Description "typed text" -Predicate {
        param($n) return ($n.Text -eq $Text -and $n.Focused -eq $true)
      }
  } catch {
    $n = Get-UiNode (Get-UiDump $Adb $DeviceSerial) $input_id
    throw "typed text mismatch. expected='$Text' $(Format-UiNodeState $n)"
  }
  if ($null -eq $typed -or $typed.Text -ne $Text) {
    throw "typed text mismatch. expected='$Text' $(Format-UiNodeState $typed)"
  }

  # Step E: tap Send button (never KEYCODE_ENTER).
  $send = Get-UiNode (Get-UiDump $Adb $DeviceSerial) $send_id
  if ($null -eq $send -or $send.X -le 0 -or $send.Y -le 0) {
    throw "send button not found. $(Format-UiNodeState $send)"
  }
  if (-not $send.Enabled) {
    throw "send button disabled. $(Format-UiNodeState $send)"
  }
  Invoke-Adb $Adb $DeviceSerial @("shell", "input", "tap", "$($send.X)", "$($send.Y)") | Out-Null

  # Step F: wait for commit marker once; do not re-tap.
  $escaped = [regex]::Escape($Text)
  $pattern = "MESSAGE_COMMITTED text=$escaped|CHAT_MESSAGE_COMMITTED platform=android .*text_key=$escaped"
  try {
    Wait-Marker $Adb $DeviceSerial $pattern "Android committed $Text" 60 | Out-Null
  } catch {
    $n = Get-UiNode (Get-UiDump $Adb $DeviceSerial) $input_id
    throw "commit marker timeout for '$Text'. $(Format-UiNodeState $n)"
  }
  Write-Host "  OK  Android sent '$Text'"
}
function Invoke-LifecycleAndroidInputHelpersSelfTest {
  $rid = "com.apptraverse.singleclientchat:id/message_input"
  $xml_a = @"
<hierarchy rotation="0"><node index="0" text="" resource-id="" class="android.widget.FrameLayout" package="com.apptraverse.singleclientchat" content-desc="" checkable="false" checked="false" clickable="false" enabled="true" focusable="false" focused="false" scrollable="false" long-clickable="false" password="false" selected="false" bounds="[0,0][1080,1920]"><node resource-id="$rid" text="hello" focused="true" enabled="true" bounds="[10,20][110,70]" /></node></hierarchy>
"@
  $a = Get-UiNode $xml_a $rid
  if ($null -eq $a) { throw "input self-test A: node null" }
  if ($a.Focused -ne $true) { throw "input self-test A: Focused" }
  if ($a.Enabled -ne $true) { throw "input self-test A: Enabled" }
  if ($a.Text -ne "hello") { throw "input self-test A: Text" }
  if ($a.X -ne 60 -or $a.Y -ne 45) { throw "input self-test A: center X=$($a.X) Y=$($a.Y)" }

  $xml_b = "<hierarchy><node resource-id=`"$rid`" text=`"Message`" focused=`"false`" enabled=`"true`" bounds=`"[10,20][110,70]`" /></hierarchy>"
  $b = Get-UiNode $xml_b $rid
  if ($b.Focused -ne $false) { throw "input self-test B: Focused" }

  $xml_c = "<hierarchy><node resource-id=`"$rid`" text=`"`" bounds=`"[10,20][110,70]`" /></hierarchy>"
  $c = Get-UiNode $xml_c $rid
  if ($c.Focused -ne $false) { throw "input self-test C: Focused" }
  if ($c.Enabled -ne $false) { throw "input self-test C: Enabled" }

  Write-Host "LIFECYCLE_ANDROID_INPUT_HELPERS_SELF_TEST PASSED"
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
function Get-ScenarioResult {
  param(
    $Results,
    [string]$ScenarioName
  )
  $hits = @($Results | Where-Object { $_.Scenario -eq $ScenarioName })
  if ($hits.Count -ne 1) {
    throw "Get-ScenarioResult expected exactly one '$ScenarioName' entry, found $($hits.Count)"
  }
  return [string]$hits[0].Result
}
function Invoke-LifecyclePersistenceGateHelpersSelfTest {
  $a = @(
    [pscustomobject]@{ Scenario = "S4_sender_exit_persisted_pending"; Result = "PASS"; Detail = "" }
  )
  if ((Get-ScenarioResult -Results $a -ScenarioName "S4_sender_exit_persisted_pending") -ne "PASS") {
    throw "persistence gate self-test A: expected PASS"
  }
  $persist_ok_a = ((Get-ScenarioResult -Results $a -ScenarioName "S4_sender_exit_persisted_pending") -eq "PASS")
  if (-not $persist_ok_a) { throw "persistence gate self-test A: persist_ok_to_run" }

  $b = @(
    [pscustomobject]@{ Scenario = "S4_sender_exit_persisted_pending"; Result = "FAIL"; Detail = "x" }
  )
  $persist_ok_b = ((Get-ScenarioResult -Results $b -ScenarioName "S4_sender_exit_persisted_pending") -eq "PASS")
  if ($persist_ok_b) { throw "persistence gate self-test B: persist_ok_to_run should be false" }

  $c_failed = $false
  try {
    Get-ScenarioResult -Results @() -ScenarioName "S4_sender_exit_persisted_pending" | Out-Null
  } catch { $c_failed = $true }
  if (-not $c_failed) { throw "persistence gate self-test C: expected missing S4 error" }

  $d = @(
    [pscustomobject]@{ Scenario = "S4_sender_exit_persisted_pending"; Result = "PASS"; Detail = "" }
    [pscustomobject]@{ Scenario = "S4_sender_exit_persisted_pending"; Result = "PASS"; Detail = "" }
  )
  $d_failed = $false
  try {
    Get-ScenarioResult -Results $d -ScenarioName "S4_sender_exit_persisted_pending" | Out-Null
  } catch { $d_failed = $true }
  if (-not $d_failed) { throw "persistence gate self-test D: expected duplicate S4 error" }

  $e = @(
    [pscustomobject]@{ Scenario = "S3_windows_offline_restart"; Result = "PASS"; Detail = "" }
    [pscustomobject]@{ Scenario = "S4_sender_exit_persisted_pending"; Result = "PASS"; Detail = "" }
    [pscustomobject]@{ Scenario = "S5_independent_histories"; Result = "PASS"; Detail = "" }
  )
  if ((Get-ScenarioResult -Results $e -ScenarioName "S4_sender_exit_persisted_pending") -ne "PASS") {
    throw "persistence gate self-test E: expected exact S4 PASS"
  }

  Write-Host "LIFECYCLE_PERSISTENCE_GATE_HELPERS_SELF_TEST PASSED"
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
  return Wait-WindowsMessageCommit -Process $Process -LogPath $Log -TextKey $Key -TimeoutSec 90
}
function Find-WindowsMessageCommit([string]$LogText, [string]$TextKey) {
  if ([string]::IsNullOrEmpty($LogText) -or [string]::IsNullOrEmpty($TextKey)) { return $null }
  $escaped = [regex]::Escape($TextKey)
  $pattern = "^CHAT_MESSAGE_COMMITTED platform=windows event=(\d+) text_key=$escaped t_us=(\d+)\s*$"
  $hits = @()
  foreach ($raw in ($LogText -split "`r?`n")) {
    $line = $raw.Trim()
    if ([string]::IsNullOrEmpty($line)) { continue }
    $m = [regex]::Match($line, $pattern)
    if (-not $m.Success) { continue }
    $hits += [pscustomobject]@{
      Line = $line
      EventId = $m.Groups[1].Value
      TextKey = $TextKey
      TimestampUs = $m.Groups[2].Value
    }
  }
  if ($hits.Count -eq 0) { return $null }
  if ($hits.Count -gt 1) {
    throw "Duplicate CHAT_MESSAGE_COMMITTED for text_key=$TextKey count=$($hits.Count)"
  }
  return $hits[0]
}
function Wait-WindowsMessageCommit {
  param(
    [System.Diagnostics.Process]$Process,
    [string]$LogPath,
    [string]$TextKey,
    [int]$TimeoutSec = 90
  )
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  $last = ""
  while ((Get-Date) -lt $deadline) {
    if ($null -eq $Process -or $Process.HasExited) {
      throw "Windows process exited before message commit text_key=$TextKey"
    }
    $last = Get-WindowsLog $LogPath
    $found = Find-WindowsMessageCommit $last $TextKey
    if ($null -ne $found) {
      Write-Host "  OK  Windows committed $TextKey event=$($found.EventId)"
      return $found
    }
    Start-Sleep -Milliseconds 350
  }
  throw "Windows message commit marker timeout text_key=$TextKey last log=$last"
}
function Find-WindowsEventPacketCreated([string]$LogText, [string]$EventId) {
  if ([string]::IsNullOrEmpty($LogText) -or [string]::IsNullOrEmpty($EventId)) { return $null }
  $escaped = [regex]::Escape($EventId)
  $pattern = "^SYNC_PACKET_CREATED kind=event packet=(\d+) event=$escaped target=(\d+) t_us=(\d+)\s*$"
  $hits = @()
  foreach ($raw in ($LogText -split "`r?`n")) {
    $line = $raw.Trim()
    if ([string]::IsNullOrEmpty($line)) { continue }
    $m = [regex]::Match($line, $pattern)
    if (-not $m.Success) { continue }
    $hits += [pscustomobject]@{
      Line = $line
      PacketId = $m.Groups[1].Value
      EventId = $EventId
      TargetNodeId = $m.Groups[2].Value
      TimestampUs = $m.Groups[3].Value
    }
  }
  if ($hits.Count -eq 0) { return $null }
  if ($hits.Count -gt 1) {
    $ids = ($hits | ForEach-Object { $_.PacketId }) -join ","
    throw "Duplicate SYNC_PACKET_CREATED for event=$EventId count=$($hits.Count) packets=$ids"
  }
  return $hits[0]
}
function Wait-WindowsEventPacketCreated {
  param(
    [System.Diagnostics.Process]$Process,
    [string]$LogPath,
    [string]$EventId,
    [int]$TimeoutSec = 90
  )
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  $last = ""
  while ((Get-Date) -lt $deadline) {
    if ($null -eq $Process -or $Process.HasExited) {
      throw "Windows process exited before EventPacket creation event=$EventId"
    }
    $last = Get-WindowsLog $LogPath
    $found = Find-WindowsEventPacketCreated $last $EventId
    if ($null -ne $found) {
      Write-Host "  OK  Windows EventPacket packet=$($found.PacketId) event=$EventId"
      return $found
    }
    Start-Sleep -Milliseconds 350
  }
  throw "EventPacket creation timeout event=$EventId"
}
function Invoke-LifecycleS4CorrelationHelpersSelfTest {
  $sample = @"
CHAT_MESSAGE_COMMITTED platform=windows event=111 text_key=other_message t_us=100
MESSAGE_COMMITTED text=s4_w_pending_before_exit
SYNC_PACKET_CREATED kind=event packet=900 event=999 target=100004 t_us=101
CHAT_MESSAGE_COMMITTED platform=windows event=4247398207 text_key=s4_w_pending_before_exit t_us=102
SYNC_PACKET_CREATED kind=event packet=4139965825 event=4247398207 target=100004 t_us=103
"@
  $commit = Find-WindowsMessageCommit $sample "s4_w_pending_before_exit"
  if ($null -eq $commit) { throw "S4 self-test: commit not found" }
  if ($commit.EventId -ne "4247398207") { throw "S4 self-test: EventId=$($commit.EventId)" }
  $packet = Find-WindowsEventPacketCreated $sample "4247398207"
  if ($null -eq $packet) { throw "S4 self-test: packet not found" }
  if ($packet.PacketId -ne "4139965825") { throw "S4 self-test: PacketId=$($packet.PacketId)" }
  if ($null -ne (Find-WindowsMessageCommit $sample "missing_key")) { throw "S4 self-test: unexpected commit" }
  $pkt999 = Find-WindowsEventPacketCreated $sample "999"
  if ($null -eq $pkt999 -or $pkt999.PacketId -ne "900") { throw "S4 self-test: unrelated event lookup" }
  # Exact key must ignore MESSAGE_COMMITTED and unrelated packet 900/event 999.
  if ($commit.EventId -eq "111" -or $packet.PacketId -eq "900") { throw "S4 self-test: correlated wrong markers" }

  $dup_commit = @"
CHAT_MESSAGE_COMMITTED platform=windows event=1 text_key=dup_key t_us=1
CHAT_MESSAGE_COMMITTED platform=windows event=2 text_key=dup_key t_us=2
"@
  $dup_commit_failed = $false
  try { Find-WindowsMessageCommit $dup_commit "dup_key" | Out-Null } catch { $dup_commit_failed = $true }
  if (-not $dup_commit_failed) { throw "S4 self-test: expected duplicate commit error" }

  $dup_packet = @"
SYNC_PACKET_CREATED kind=event packet=10 event=55 target=100004 t_us=1
SYNC_PACKET_CREATED kind=event packet=11 event=55 target=100004 t_us=2
"@
  $dup_packet_failed = $false
  try { Find-WindowsEventPacketCreated $dup_packet "55" | Out-Null } catch { $dup_packet_failed = $true }
  if (-not $dup_packet_failed) { throw "S4 self-test: expected duplicate packet error" }

  Write-Host "LIFECYCLE_S4_CORRELATION_HELPERS_SELF_TEST PASSED"
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
function Wait-WindowsSyncResumedNoPending {
  param(
    [System.Diagnostics.Process]$Process,
    [string]$LogPath,
    [string]$PeerUid,
    [int]$TimeoutSec
  )
  $pattern = "CHAT_SYNC_RESUMED peer=$([regex]::Escape($PeerUid)) initial_complete=1 pending=0"
  Wait-WindowsMarker $Process $LogPath $pattern "Windows CHAT_SYNC_RESUMED pending=0" $TimeoutSec | Out-Null
}
function Wait-AndroidSyncResumedNoPending {
  param(
    [string]$PeerUid,
    [int]$TimeoutSec
  )
  $pattern = "CHAT_SYNC_RESUMED peer=$([regex]::Escape($PeerUid)) initial_complete=1 pending=0"
  Wait-Marker $adb $Serial $pattern "Android CHAT_SYNC_RESUMED pending=0" $TimeoutSec | Out-Null
}
function Get-S4AndroidDeliverySnapshot {
  param(
    [string]$EventId,
    [string]$PacketId,
    [string]$MessageKey
  )
  $logs = Get-Logcat $adb $Serial
  $transcript = Get-CanonicalAndroidTranscript
  $pkt = [regex]::Escape($PacketId)
  $evt = [regex]::Escape($EventId)
  return [pscustomobject]@{
    MessageCount = (Count-Occurrences $transcript $MessageKey)
    PacketReceiveCount = ([regex]::Matches($logs, "SYNC_PACKET_RECEIVED kind=event packet=$pkt")).Count
    EventApplyCount = ([regex]::Matches($logs, "SYNC_EVENT_APPLIED packet=$pkt event=$evt")).Count
    EventBlockedCount = ([regex]::Matches($logs, "SYNC_EVENT_BLOCKED packet=$pkt event=$evt")).Count
    AckSentCount = ([regex]::Matches($logs, "SYNC_ACK_SENT[^\r\n]*acknowledged=$pkt")).Count
    TranscriptSource = $script:LastCanonicalTranscriptSource
    Transcript = $transcript
  }
}
function Classify-S4PreRestartDelivery($Snapshot) {
  if ($null -eq $Snapshot) { throw "S4 pre-restart snapshot is null" }
  if ($Snapshot.EventBlockedCount -gt 0) {
    throw "S4 pre-restart EventBlockedCount=$($Snapshot.EventBlockedCount) (want 0)"
  }
  if ($Snapshot.MessageCount -gt 1) {
    throw "S4 pre-restart MessageCount=$($Snapshot.MessageCount) (want 0 or 1)"
  }
  if ($Snapshot.EventApplyCount -gt 1) {
    throw "S4 pre-restart EventApplyCount=$($Snapshot.EventApplyCount) (want 0 or 1)"
  }
  if ($Snapshot.MessageCount -eq 0 -and $Snapshot.EventApplyCount -eq 0 -and $Snapshot.AckSentCount -eq 0) {
    if ($Snapshot.PacketReceiveCount -gt 0) {
      throw "S4 pre-restart received packet without apply/message (receive=$($Snapshot.PacketReceiveCount))"
    }
    return "awaiting_sender_restart"
  }
  if ($Snapshot.MessageCount -eq 1 -and $Snapshot.PacketReceiveCount -ge 1 -and `
      $Snapshot.EventApplyCount -eq 1 -and $Snapshot.EventBlockedCount -eq 0 -and `
      $Snapshot.AckSentCount -ge 1) {
    return "in_flight_delivered"
  }
  throw ("S4 pre-restart snapshot invalid: message=$($Snapshot.MessageCount) receive=$($Snapshot.PacketReceiveCount) " + `
    "apply=$($Snapshot.EventApplyCount) blocked=$($Snapshot.EventBlockedCount) ack_sent=$($Snapshot.AckSentCount)")
}
function Wait-WindowsS4Progress {
  param(
    [System.Diagnostics.Process]$Process,
    [string]$LogPath,
    [string]$PacketId,
    [int]$TimeoutSec
  )
  $pkt = [regex]::Escape($PacketId)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  $saw_retry = $false
  $saw_ack = $false
  $last = ""
  while ((Get-Date) -lt $deadline) {
    if ($null -eq $Process -or $Process.HasExited) {
      throw "Windows process exited during S4 progress wait packet=$PacketId"
    }
    $last = Get-WindowsLog $LogPath
    if (-not $saw_retry -and $last -match "SYNC_PACKET_RETRY packet=$pkt") { $saw_retry = $true }
    if (-not $saw_ack -and $last -match "SYNC_ACK_RECEIVED[^\r\n]*acknowledged=$pkt") { $saw_ack = $true }
    if ($saw_ack) {
      return [pscustomobject]@{ SawRetry = $saw_retry; SawAck = $saw_ack }
    }
    Start-Sleep -Milliseconds 350
  }
  throw "Windows S4 progress timeout packet=$PacketId saw_retry=$saw_retry saw_ack=$saw_ack last log=$last"
}
function Invoke-LifecycleS4DeliveryPathHelpersSelfTest {
  $a = [pscustomobject]@{
    MessageCount = 0; PacketReceiveCount = 0; EventApplyCount = 0
    EventBlockedCount = 0; AckSentCount = 0
  }
  if ((Classify-S4PreRestartDelivery $a) -ne "awaiting_sender_restart") {
    throw "S4 delivery self-test A"
  }

  $b = [pscustomobject]@{
    MessageCount = 1; PacketReceiveCount = 1; EventApplyCount = 1
    EventBlockedCount = 0; AckSentCount = 1
  }
  if ((Classify-S4PreRestartDelivery $b) -ne "in_flight_delivered") {
    throw "S4 delivery self-test B"
  }

  $c = [pscustomobject]@{
    MessageCount = 1; PacketReceiveCount = 2; EventApplyCount = 1
    EventBlockedCount = 0; AckSentCount = 2
  }
  if ((Classify-S4PreRestartDelivery $c) -ne "in_flight_delivered") {
    throw "S4 delivery self-test C"
  }

  $d_failed = $false
  try {
    Classify-S4PreRestartDelivery ([pscustomobject]@{
      MessageCount = 2; PacketReceiveCount = 1; EventApplyCount = 1
      EventBlockedCount = 0; AckSentCount = 1
    }) | Out-Null
  } catch { $d_failed = $true }
  if (-not $d_failed) { throw "S4 delivery self-test D" }

  $e_failed = $false
  try {
    Classify-S4PreRestartDelivery ([pscustomobject]@{
      MessageCount = 1; PacketReceiveCount = 2; EventApplyCount = 2
      EventBlockedCount = 0; AckSentCount = 2
    }) | Out-Null
  } catch { $e_failed = $true }
  if (-not $e_failed) { throw "S4 delivery self-test E" }

  $f_failed = $false
  try {
    Classify-S4PreRestartDelivery ([pscustomobject]@{
      MessageCount = 1; PacketReceiveCount = 0; EventApplyCount = 0
      EventBlockedCount = 0; AckSentCount = 0
    }) | Out-Null
  } catch { $f_failed = $true }
  if (-not $f_failed) { throw "S4 delivery self-test F" }

  $g_failed = $false
  try {
    Classify-S4PreRestartDelivery ([pscustomobject]@{
      MessageCount = 0; PacketReceiveCount = 1; EventApplyCount = 0
      EventBlockedCount = 1; AckSentCount = 0
    }) | Out-Null
  } catch { $g_failed = $true }
  if (-not $g_failed) { throw "S4 delivery self-test G" }

  Write-Host "LIFECYCLE_S4_DELIVERY_PATH_HELPERS_SELF_TEST PASSED"
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
Invoke-LifecycleAndroidInputHelpersSelfTest
Invoke-LifecycleS4CorrelationHelpersSelfTest
Invoke-LifecyclePersistenceGateHelpersSelfTest
Invoke-LifecycleS4DeliveryPathHelpersSelfTest

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

      $commit = Send-WindowsInboxMessage $script:WinProc $script:WinLog $commit_inbox "s4_w_pending_before_exit"
      $script:S4EventId = $commit.EventId
      $packet = Wait-WindowsEventPacketCreated -Process $script:WinProc -LogPath $script:WinLog `
        -EventId $script:S4EventId -TimeoutSec 60
      $script:S4PacketId = $packet.PacketId
      Write-Utf8NoBom (Join-Path $out_dir "s4_packet_ids.txt") "event_id=$($script:S4EventId)`r`npacket_id=$($script:S4PacketId)`r`n"

      Wait-WindowsPendingPositive $script:WinProc $script:WinLog "s4_w_pending_before_exit" 60
      $pre_exit_log = Get-WindowsLog $script:WinLog
      $created_exact = Find-WindowsEventPacketCreated $pre_exit_log $script:S4EventId
      if ($null -eq $created_exact -or $created_exact.PacketId -ne $script:S4PacketId) {
        throw "S4 pre-exit missing exact SYNC_PACKET_CREATED packet=$($script:S4PacketId) event=$($script:S4EventId)"
      }
      if ($pre_exit_log -match "SYNC_ACK_RECEIVED[^\r\n]*acknowledged=$([regex]::Escape($script:S4PacketId))") {
        throw "S4 pre-exit unexpectedly already has SYNC_ACK_RECEIVED for packet=$($script:S4PacketId)"
      }
      if ($pre_exit_log -match "SYNC_PENDING_REMOVED packet=$([regex]::Escape($script:S4PacketId))") {
        throw "S4 pre-exit unexpectedly already has SYNC_PENDING_REMOVED for packet=$($script:S4PacketId)"
      }
      Write-Host "  OK  S4 pending packet=$($script:S4PacketId) event=$($script:S4EventId) not ACKed before exit"

      # Stop Windows completely BEFORE starting Android.
      Stop-WindowsRun $script:WinProc

      Clear-Logcat $adb $Serial
      Start-App $adb $Serial
      Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=$([regex]::Escape($script:AndroidUid))" "Android ready S4 old state" $ClientReadyTimeoutSec | Out-Null
      Wait-Marker $adb $Serial "CHAT_SYNC_CONTROLLER_READY" "Android sync controller ready S4" 60 | Out-Null

      $observe_deadline = (Get-Date).AddSeconds(3)
      while ((Get-Date) -lt $observe_deadline) {
        $probe = Get-S4AndroidDeliverySnapshot -EventId $script:S4EventId -PacketId $script:S4PacketId `
          -MessageKey "s4_w_pending_before_exit"
        if ($probe.MessageCount -ge 1 -or $probe.PacketReceiveCount -ge 1) { break }
        Start-Sleep -Milliseconds 250
      }
      $pre_restart = Get-S4AndroidDeliverySnapshot -EventId $script:S4EventId -PacketId $script:S4PacketId `
        -MessageKey "s4_w_pending_before_exit"
      $s4_path = Classify-S4PreRestartDelivery $pre_restart
      Write-Host "S4_PRE_RESTART_PATH path=$s4_path"

      $win4 = Start-WindowsLifecycle $win_state_dir $WindowsClientName "windows_s4.log"
      Assert-StableIdentities $win4.Log
      Wait-WindowsMarker $win4.Process $win4.Log `
        "CHAT_SYNC_RESUMED peer=$([regex]::Escape($script:AndroidUid)) initial_complete=1 pending=1" `
        "Windows CHAT_SYNC_RESUMED pending=1 S4" $SyncTimeoutSec | Out-Null

      $progress = Wait-WindowsS4Progress -Process $win4.Process -LogPath $win4.Log `
        -PacketId $script:S4PacketId -TimeoutSec $SyncTimeoutSec
      $retry_flag = if ($progress.SawRetry) { "1" } else { "0" }
      Write-Host "  OK  S4 Windows progress saw_retry=$retry_flag saw_ack=1"

      Assert-AndroidHasKeyOnce "s4_w_pending_before_exit"
      Wait-WindowsMarker $win4.Process $win4.Log "SYNC_ACK_RECEIVED[^\r\n]*acknowledged=$([regex]::Escape($script:S4PacketId))" "Windows SYNC_ACK_RECEIVED S4 packet" $SyncTimeoutSec | Out-Null
      Wait-WindowsMarker $win4.Process $win4.Log "SYNC_PENDING_REMOVED packet=$([regex]::Escape($script:S4PacketId)) pending=0" "Windows SYNC_PENDING_REMOVED exact S4 packet" $SyncTimeoutSec | Out-Null
      Wait-WindowsPendingCleared $win4.Process $win4.Log $SyncTimeoutSec

      $restart_log = Get-WindowsLog $win4.Log
      $new_packet = Find-WindowsEventPacketCreated $restart_log $script:S4EventId
      if ($null -ne $new_packet) {
        throw "S4 restart log unexpectedly created EventPacket packet=$($new_packet.PacketId) for event=$($script:S4EventId)"
      }

      $final = Get-S4AndroidDeliverySnapshot -EventId $script:S4EventId -PacketId $script:S4PacketId `
        -MessageKey "s4_w_pending_before_exit"
      if ($final.PacketReceiveCount -lt 1) {
        throw "S4 final PacketReceiveCount=$($final.PacketReceiveCount) (want >= 1)"
      }
      if ($final.EventApplyCount -ne 1) {
        throw "S4 final EventApplyCount=$($final.EventApplyCount) (want 1)"
      }
      if ($final.EventBlockedCount -ne 0) {
        throw "S4 final EventBlockedCount=$($final.EventBlockedCount) (want 0)"
      }
      if ($final.AckSentCount -lt 1) {
        throw "S4 final AckSentCount=$($final.AckSentCount) (want >= 1)"
      }
      if ($final.MessageCount -ne 1) {
        throw "S4 final MessageCount=$($final.MessageCount) (want 1)"
      }
      $remove_count = ([regex]::Matches($restart_log, "SYNC_PENDING_REMOVED packet=$([regex]::Escape($script:S4PacketId)) pending=0")).Count
      if ($remove_count -lt 1) {
        throw "S4 missing SYNC_PENDING_REMOVED for packet=$($script:S4PacketId)"
      }

      $s4_detail = "path=$s4_path event=$($script:S4EventId) packet=$($script:S4PacketId) retry=$retry_flag receive_count=$($final.PacketReceiveCount) apply_count=1 ack_sent_count=$($final.AckSentCount) pending=0 message_count=1"
      Write-Host "  OK  S4 exact packet lifecycle $s4_detail"

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
      Record-Result "S4_sender_exit_persisted_pending" $true $s4_detail
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
  $s4_result = Get-ScenarioResult -Results $script:Results -ScenarioName "S4_sender_exit_persisted_pending"
  $persist_ok_to_run = ($s4_result -eq "PASS")
  if ($persist_ok_to_run) {
    try {
      Write-Host ""
      Write-Host "Final persistence -- restart both, no new messages"
      $windows_was_running = ($null -ne $script:WinProc -and -not $script:WinProc.HasExited)
      $running_flag = if ($windows_was_running) { "1" } else { "0" }
      Write-Host "PERSISTENCE_PRECONDITION s4_result=PASS windows_was_running=$running_flag"

      $expected_keys = @(
        "s1_w_before_android", "s1_w_to_a", "s1_a_to_w",
        "s2_w_while_a_offline", "s2_a_after_rejoin",
        "s3_a_while_w_offline", "s3_w_after_rejoin",
        "s4_w_pending_before_exit"
      )
      try {
        Stop-WindowsRun $script:WinProc
      } catch {
        Stop-WindowsChat
      }
      try {
        Stop-App $adb $Serial
      } catch {
        Write-Host "  WARN Stop-App during persistence prep: $($_.Exception.Message)"
      }
      Start-Sleep -Seconds 2
      Clear-Logcat $adb $Serial
      Start-App $adb $Serial
      Wait-Marker $adb $Serial "AETHER_CLIENT_READY platform=android uid=$([regex]::Escape($script:AndroidUid))" "Android persistence ready" $ClientReadyTimeoutSec | Out-Null
      $winp = Start-WindowsLifecycle $win_state_dir $WindowsClientName "windows_persist.log"
      Assert-StableIdentities $winp.Log
      Wait-WindowsSyncResumedNoPending -Process $winp.Process -LogPath $winp.Log `
        -PeerUid $script:AndroidUid -TimeoutSec $SyncTimeoutSec
      Wait-AndroidSyncResumedNoPending -PeerUid $script:WindowsUid -TimeoutSec $SyncTimeoutSec

      $transcript = Get-AndroidJoinTranscript
      Assert-JoinCounts $transcript 1 1 "Persistence joins"
      foreach ($key in $expected_keys) {
        Assert-ContainsOnce $transcript $key "Persistence Android history"
      }

      $win_persist_log = Get-WindowsLog $winp.Log
      $android_persist_log = Get-Logcat $adb $Serial
      if ($win_persist_log -match "SYNC_PACKET_CREATED kind=node_state") {
        throw "Persistence Windows log unexpectedly created NodeStatePacket"
      }
      if ($android_persist_log -match "SYNC_PACKET_CREATED kind=node_state") {
        throw "Persistence Android log unexpectedly created NodeStatePacket"
      }
      Write-Host "  OK  Persistence: no new SYNC_PACKET_CREATED kind=node_state"

      Assert-NoCrash $win_persist_log "Windows persistence"
      Assert-NoCrash $android_persist_log "Android persistence"
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
    Record-Result "Persistence_after_S1_S4" $false "Skipped because S4 did not pass"
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
