param(
  [string]$Exe = (Join-Path $PSScriptRoot '../../build/shared-node-debug/examples/shared_node_chat/shared_node_chat.exe'),
  [string]$Profiles = (Join-Path $PSScriptRoot '../../.artifacts/shared-chat-demo')
)
$ErrorActionPreference = 'Stop'
$chatExe = (Resolve-Path -LiteralPath $Exe).Path
$chatProfiles = [IO.Path]::GetFullPath($Profiles)
foreach ($participant in @('Host', 'Alice', 'Bob')) {
  $chatProfile = Join-Path $chatProfiles $participant
  # These are the three visible chat windows requested for the demonstration.
  $chatRole = if ($participant -eq 'Host') { '--host' } else { '--client' }
  Start-Process -FilePath $chatExe -ArgumentList @($chatRole, '--profile', ('"' + $chatProfile + '"'), '--name', $participant)
}
Write-Host 'Host starts automatically. Copy its Aether ID into the clients and click Join.'
