param(
    [Parameter(Mandatory)][ValidatePattern('\A[a-zA-Z_][a-zA-Z0-9_.-]*@[a-zA-Z0-9][a-zA-Z0-9.-]*\z')][string]$Remote,
    [ValidatePattern('\A[a-z][a-z0-9-]{0,47}\z')][string]$App = "lifecycle-demo",
    [ValidateRange(0, 3)][int]$Through = 3,
    [string]$BuildDirectory = "build/win-lifecycle",
    [string]$IdentityFile = "",
    [switch]$NoBuild
)
$ErrorActionPreference = "Stop"
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    if (!$NoBuild) { & "$PSScriptRoot/build_update_lab.ps1" -BuildDirectory $BuildDirectory -BuildOnly }
    $bin = (Resolve-Path -LiteralPath "$BuildDirectory/examples/update_lab/Debug").Path
    $files = @("update_lab_launcher.exe", "update_lab_e0.exe")
    if ($Through -ge 3) { $files += "update_lab_e1.exe" }
    $paths = @($files | ForEach-Object { (Resolve-Path -LiteralPath (Join-Path $bin $_)).Path })
    $options = @("-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes", "-o", "ConnectTimeout=10")
    if ($IdentityFile) { $options += @("-o", "IdentitiesOnly=yes", "-i", (Resolve-Path -LiteralPath $IdentityFile).Path) }

    $stage = [string](& ssh @options $Remote "mktemp -d /tmp/apptraverse-lifecycle.XXXXXXXXXX")
    if ($LASTEXITCODE -or $stage -cnotmatch '\A/tmp/apptraverse-lifecycle\.[a-zA-Z0-9]{10}\z') {
        throw "Could not create the server's staging directory"
    }
    $stagedFiles = ($files | ForEach-Object { "$stage/$_" }) -join " "
    try {
        & scp @options @paths "${Remote}:$stage/"
        if ($LASTEXITCODE) { throw "Upload to the test server failed" }
        $prefix = "/opt/apptraverse-lifecycle"
        $publish = "set -eu; sudo -n chgrp -- apptraverse-lifecycle $stage $stagedFiles; chmod 0750 -- $stage; chmod 0640 -- $stagedFiles; "
        $publish += "sudo -n -u apptraverse-lifecycle -- $prefix/runtime/ld-linux-x86-64.so.2 --library-path $prefix/runtime $prefix/update_lab_provider publish --out /var/lib/apptraverse-lifecycle-store --app $App --through $Through"
        $publish += " --windows-x64-launcher $stage/update_lab_launcher.exe --windows-x64-e0 $stage/update_lab_e0.exe"
        if ($Through -ge 3) { $publish += " --windows-x64-e1 $stage/update_lab_e1.exe" }
        & ssh @options $Remote $publish
        if ($LASTEXITCODE) { throw "Service-side publication failed" }
    } finally {
        & ssh @options $Remote "rm -f -- $stagedFiles && rmdir -- $stage"
        if ($LASTEXITCODE) { Write-Warning "Could not remove staging directory $stage on $Remote" }
    }
} finally { Pop-Location }
