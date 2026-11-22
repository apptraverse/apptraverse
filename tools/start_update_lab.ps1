param(
    [string]$BuildDirectory = "build/win-update-lab",
    [string]$Profile = "artifacts/update-lab/device-windows",
    [string]$Installation = "artifacts/update-lab/installation-windows",
    [switch]$FileSource
)
$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
    $binaries = (Resolve-Path "$BuildDirectory/examples/update_lab/Debug").Path
    $store = (Resolve-Path "artifacts/update-lab/store").Path
    $arguments = @("--install", [IO.Path]::GetFullPath($Installation), "--profile", [IO.Path]::GetFullPath($Profile), "--seed", "$binaries/update_lab_e0.exe")
    if ($FileSource) { $arguments += @("--store", $store) }
    else {
        $providerProfile = [IO.Path]::GetFullPath("artifacts/update-lab/component-client")
        New-Item -ItemType Directory -Path $providerProfile -Force | Out-Null
        $endpoint = Join-Path $providerProfile "endpoint.txt"
        # Identity and its UID survive provider restarts. A second provider exits at its lock.
        $providerArguments = @("--store", ('"' + $store + '"'), "--profile", ('"' + $providerProfile + '"'), "--endpoint-file", ('"' + $endpoint + '"'))
        Start-Process -FilePath "$binaries/update_lab_provider.exe" -ArgumentList $providerArguments -WindowStyle Hidden `
            -RedirectStandardOutput "$providerProfile/provider.log" -RedirectStandardError "$providerProfile/provider-error.log" | Out-Null
        $deadline = [DateTime]::UtcNow.AddSeconds(120)
        while (-not (Test-Path -LiteralPath $endpoint)) {
            if ([DateTime]::UtcNow -gt $deadline) { throw "The component client did not obtain an Aether UID; see $providerProfile/provider-error.log" }
            Start-Sleep -Milliseconds 100
        }
        $arguments += @("--provider", (Get-Content -LiteralPath $endpoint -Raw).Trim())
    }
    & "$binaries/update_lab_launcher.exe" @arguments
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 3) { throw "Application exited with $LASTEXITCODE" }
} finally { Pop-Location }
