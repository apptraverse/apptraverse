Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Win {
  public const uint WM_SETTEXT = 0x000C;
  public const uint BM_CLICK = 0x00F5;
  public const uint WM_GETTEXT = 0x000D;
  public const uint WM_GETTEXTLENGTH = 0x000E;
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string w);
  [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr c, string cls, string text);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
}
"@

$repo = 'C:\Users\nickc\Projects\AppTraverse-mcp'
$exe = "$repo\build\win64-ninja-msvc-release\examples\chat_ui_runtime_demo\windows\win32_chat_ui_runtime_demo.exe"
Get-Process win32_chat_ui_runtime_demo -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 1
Remove-Item -Recurse -Force "$repo\host_state","$repo\client_state" -ErrorAction SilentlyContinue
& $exe --distill "$repo\host_state" --name Host
& $exe --distill "$repo\client_state" --name Client
Start-Sleep 3

$hostProc = Start-Process -FilePath $exe -ArgumentList @('--host','--state-dir',"$repo\host_state") -WorkingDirectory $repo -PassThru
Start-Sleep 5
$clientProc = Start-Process -FilePath $exe -ArgumentList @('--client','--state-dir',"$repo\client_state") -WorkingDirectory $repo -PassThru
Start-Sleep 10
Write-Output "host_pid=$($hostProc.Id) exited=$($hostProc.HasExited) client_pid=$($clientProc.Id) exited=$($clientProc.HasExited)"

function Wait-Uid($path, $timeoutSec=30) {
  $deadline = (Get-Date).AddSeconds($timeoutSec)
  while ((Get-Date) -lt $deadline) {
    if (Test-Path $path) {
      $t = (Get-Content $path -Raw).Trim()
      if ($t.Length -gt 10) { return $t }
    }
    Start-Sleep -Milliseconds 500
  }
  return $null
}

$hostUid = Wait-Uid "$repo\host_state\aether\last_uid.txt"
$clientUid = Wait-Uid "$repo\client_state\aether\last_uid.txt"
Write-Output "HOST_UID=$hostUid"
Write-Output "CLIENT_UID=$clientUid"
if (-not $hostUid -or -not $clientUid) { throw 'UIDs missing' }

function Get-ChatWindows {
  Get-Process win32_chat_ui_runtime_demo | ForEach-Object {
    $_.MainWindowHandle
  } | Where-Object { $_ -ne [IntPtr]::Zero }
}

$windows = @(Get-ChatWindows)
Write-Output "windows=$($windows.Count)"

function Find-Controls($hwnd) {
  $edits = New-Object System.Collections.Generic.List[IntPtr]
  $buttons = New-Object System.Collections.Generic.List[object]
  $cb = [Win+EnumProc]{
    param($h,$l)
    $cls = New-Object System.Text.StringBuilder 256
    [void][Win]::GetClassName($h, $cls, 256)
    $txt = New-Object System.Text.StringBuilder 256
    [void][Win]::GetWindowText($h, $txt, 256)
    if ($cls.ToString() -eq 'Edit') { $edits.Add($h) }
    if ($cls.ToString() -eq 'Button') { $buttons.Add([pscustomobject]@{H=$h; T=$txt.ToString()}) }
    return $true
  }
  [void][Win]::EnumChildWindows($hwnd, $cb, [IntPtr]::Zero)
  return [pscustomobject]@{ Edits=$edits; Buttons=$buttons }
}

# Identify client window: has Connect button
$clientHwnd = [IntPtr]::Zero
$hostHwnd = [IntPtr]::Zero
foreach ($w in $windows) {
  $c = Find-Controls $w
  $hasConnect = $false
  foreach ($b in $c.Buttons) { if ($b.T -eq 'Connect') { $hasConnect = $true } }
  if ($hasConnect) { $clientHwnd = $w } else { $hostHwnd = $w }
}
Write-Output "clientHwnd=$clientHwnd hostHwnd=$hostHwnd"
if ($clientHwnd -eq [IntPtr]::Zero) { throw 'client window not found' }

# Send pre-connect messages via message input (usually last Edit on both)
function Send-ChatMessage($hwnd, $text) {
  $c = Find-Controls $hwnd
  if ($c.Edits.Count -lt 1) { throw 'no edits' }
  $input = $c.Edits[$c.Edits.Count - 1]
  [void][Win]::SendMessage($input, [Win]::WM_SETTEXT, [IntPtr]::Zero, $text)
  foreach ($b in $c.Buttons) {
    if ($b.T -eq 'Send') {
      [void][Win]::SendMessage($b.H, [Win]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
      return
    }
  }
  throw 'Send button missing'
}

Send-ChatMessage $hostHwnd 'host before'
Start-Sleep 1
Send-ChatMessage $clientHwnd 'client before'
Start-Sleep 1

# Client: set Host UID and Connect
$cc = Find-Controls $clientHwnd
# Host UID edit is the connection-bar edit (first Edit for client)
$hostEdit = $cc.Edits[0]
[void][Win]::SendMessage($hostEdit, [Win]::WM_SETTEXT, [IntPtr]::Zero, $hostUid)
foreach ($b in $cc.Buttons) {
  if ($b.T -eq 'Connect') {
    [void][Win]::SendMessage($b.H, [Win]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
  }
}

Write-Output 'Connect clicked, waiting for convergence...'
Start-Sleep 20

function Count-Log($dir, $pattern) {
  $log = Join-Path $dir 'chat_runtime.log'
  if (-not (Test-Path $log)) { return 0 }
  return (Select-String -Path $log -Pattern $pattern -SimpleMatch | Measure-Object).Count
}

$hostReady = Count-Log "$repo\host_state" 'SHARED_STREAM_READY'
$clientReady = Count-Log "$repo\client_state" 'SHARED_STREAM_READY'
$hostApplied = Count-Log "$repo\host_state" 'SHARED_EVENT_APPLIED'
$clientApplied = Count-Log "$repo\client_state" 'SHARED_EVENT_APPLIED'
$hostAckRecv = Count-Log "$repo\host_state" 'SHARED_ACK_RECEIVED'
$clientAckRecv = Count-Log "$repo\client_state" 'SHARED_ACK_RECEIVED'
$hostEventRecv = Count-Log "$repo\host_state" 'SHARED_EVENT_RECEIVED'
$clientEventRecv = Count-Log "$repo\client_state" 'SHARED_EVENT_RECEIVED'

Write-Output "hostReady=$hostReady clientReady=$clientReady"
Write-Output "hostApplied=$hostApplied clientApplied=$clientApplied"
Write-Output "hostAckRecv=$hostAckRecv clientAckRecv=$clientAckRecv"
Write-Output "hostEventRecv=$hostEventRecv clientEventRecv=$clientEventRecv"

Write-Output '--- host log tail ---'
if (Test-Path "$repo\host_state\chat_runtime.log") {
  Get-Content "$repo\host_state\chat_runtime.log" -Tail 40
}
Write-Output '--- client log tail ---'
if (Test-Path "$repo\client_state\chat_runtime.log") {
  Get-Content "$repo\client_state\chat_runtime.log" -Tail 40
}

$pass = ($hostReady -ge 1 -or $clientReady -ge 1) -and ($hostApplied -ge 1) -and ($clientApplied -ge 1) -and ($hostAckRecv -ge 1) -and ($clientAckRecv -ge 1)
Write-Output "PASS=$pass"
if (-not $pass) { exit 1 }
