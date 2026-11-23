param(
    [string]$BuildDirectory = "build/win-update-lab",
    [string]$Profile = "artifacts/update-lab/distribution-device-windows",
    [string]$Installation = "artifacts/update-lab/distribution-installation-windows",
    [ValidateRange(0, 3)][int]$InitialRelease = 0,
    [string]$StoreDirectory = "artifacts/update-lab/device-releases",
    [switch]$FileSource
)
$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
    $binaries = (Resolve-Path "$BuildDirectory/examples/update_lab/Debug").Path
    $store = (Resolve-Path -LiteralPath $StoreDirectory).Path
    $arguments = @("--install", [IO.Path]::GetFullPath($Installation), "--profile", [IO.Path]::GetFullPath($Profile), "--image", (Join-Path $store "images/R$InitialRelease"))
    if ($FileSource) { $arguments += @("--store", $store) }
    else {
        $providerProfile = "$store-component-client"
        New-Item -ItemType Directory -Path $providerProfile -Force | Out-Null
        $endpoint = Join-Path $providerProfile "endpoint.txt"
        # The provider owns an exclusive file handle for the lifetime of this store's service.
        $running = $false
        try {
            $lock = [IO.File]::Open((Join-Path $providerProfile "provider.lock"), 'OpenOrCreate', 'ReadWrite', 'None')
            $lock.Dispose()
        } catch [IO.IOException] {
            if (($_.Exception.HResult -band 0xffff) -ne 32) { throw }
            $running = $true
        }
        if (!$running) {
            $providerArguments = @("--store", ('"' + $store + '"'), "--profile", ('"' + $providerProfile + '"'), "--endpoint-file", ('"' + $endpoint + '"'))
            $providerProcess = Start-Process -FilePath "$binaries/update_lab_provider.exe" -ArgumentList $providerArguments -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput "$providerProfile/provider.log" -RedirectStandardError "$providerProfile/provider-error.log"
        }
        $deadline = [DateTime]::UtcNow.AddSeconds(120)
        while (-not (Test-Path -LiteralPath $endpoint)) {
            if ($providerProcess -and $providerProcess.HasExited) { throw "The component client exited. See $providerProfile/provider-error.log" }
            if ([DateTime]::UtcNow -gt $deadline) { throw "The component client did not obtain an Aether UID; see $providerProfile/provider-error.log" }
            Start-Sleep -Milliseconds 100
        }
        $arguments += @("--provider", (Get-Content -LiteralPath $endpoint -Raw).Trim())
    }
    & "$binaries/update_lab_launcher.exe" @arguments
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 3) { throw "Application exited with $LASTEXITCODE" }
} finally { Pop-Location }
