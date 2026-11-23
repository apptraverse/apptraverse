param(
    [Parameter(Mandatory)][ValidatePattern('\A[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}\z')][string]$Provider,
    [ValidatePattern('\A[a-z][a-z0-9-]{0,47}\z')][string]$App = "lifecycle-demo",
    [string]$BuildDirectory = "build/win-lifecycle",
    [string]$DeviceRoot = "$env:LOCALAPPDATA/AppTraverse/LifecycleDemo"
)
$ErrorActionPreference = "Stop"
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $bin = (Resolve-Path -LiteralPath "$BuildDirectory/examples/update_lab/Debug").Path
    $device = [IO.Path]::GetFullPath((Join-Path $DeviceRoot "app-$App"))
    $installation = Join-Path $device "installation"
    $profile = Join-Path $device "profile"
    $argsForLauncher = @("--app", $App, "--profile", $profile, "--install", $installation, "--provider", $Provider)
    if (!(Test-Path -LiteralPath "$installation/installation.bin")) {
        $bootstrap = Join-Path $device "bootstrap"
        $image = Join-Path $bootstrap "apps/app-$App/images/R0"
        if (!(Test-Path -LiteralPath "$image/windows-x64/update_lab.exe") -or
            !(Test-Path -LiteralPath "$image/full.ats") -or !(Test-Path -LiteralPath "$image/release.bin")) {
            & "$bin/update_lab_pack.exe" publish --out $bootstrap --app $App --through 0 --windows-x64-e0 "$bin/update_lab_e0.exe"
            if ($LASTEXITCODE) { throw "Could not prepare the R0 bootstrap image" }
        }
        $argsForLauncher += @("--image", $image)
    }
    & "$bin/update_lab_launcher.exe" @argsForLauncher
    if ($LASTEXITCODE -notin 0, 3) { throw "Application exited with $LASTEXITCODE" }
} finally { Pop-Location }
