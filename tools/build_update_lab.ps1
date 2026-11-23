param(
    [string]$BuildDirectory = "build/win-update-lab",
    [string]$SdkSource = "",
    [string]$ObjectsSource = "",
    [string]$TelemetrySource = "",
    [string]$LinuxBinaries = "",
    [ValidateRange(0, 3)][int]$Through = 3,
    [string]$Store = "artifacts/update-lab/lzma1-releases",
    [string]$Cache = "C:/cpm",
    [switch]$BuildOnly
)
$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
    $configure = @("-S", ".", "-B", $BuildDirectory, "-G", "Visual Studio 17 2022", "-A", "x64",
        "-DCMAKE_CONFIGURATION_TYPES=Debug", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug",
        "-DAPPTRAVERSE_BUILD_EXAMPLES=OFF", "-DBUILD_TESTING=OFF", "-DAPPTRAVERSE_BUILD_UPDATE_LAB=ON",
        "-DAPPTRAVERSE_BUILD_AETHER_LINK=ON", "-DAPPTRAVERSE_AETHER_STREAM_HIERARCHY=OFF",
        "-DAPPTRAVERSE_AETHER_USE_WORKSPACE_SOURCE=OFF",
        "-DAPPTRAVERSE_AETHER_USER_CONFIG=$repo/cmake/update_lab_aether_config.h",
        "-DAE_NO_DEBUG_LOG=ON", "-DAE_OBJECTS_RAW_TELE=OFF", "-DCPM_SOURCE_CACHE=$Cache")
    if ($SdkSource) { $configure += "-DAPPTRAVERSE_AETHER_CLIENT_SOURCE=$SdkSource" }
    if ($ObjectsSource) { $configure += "-DCPM_aether-objects_SOURCE=$ObjectsSource" }
    if ($TelemetrySource) { $configure += "-DCPM_aether-tele_SOURCE=$TelemetrySource" }
    & cmake @configure
    if ($LASTEXITCODE) { throw "CMake configure failed" }
    & cmake --build $BuildDirectory --config Debug --target update_lab_pack update_lab_provider update_lab_launcher update_lab_e0 update_lab_e1 update_lab_e2 update_lab_state_test -j 8
    if ($LASTEXITCODE) { throw "Build failed" }
    & "$BuildDirectory/examples/update_lab/Debug/update_lab_state_test.exe"
    if ($LASTEXITCODE) { throw "State tests failed" }
    if ($BuildOnly) { return }
    $binaries = (Resolve-Path "$BuildDirectory/examples/update_lab/Debug").Path
    $publish = @('publish', '--out', $Store, '--through', $Through,
        '--windows-x64-launcher', "$binaries/update_lab_launcher.exe", '--windows-x64-e0', "$binaries/update_lab_e0.exe", '--windows-x64-e1', "$binaries/update_lab_e1.exe", '--windows-x64-e2', "$binaries/update_lab_e2.exe")
    $generated = Join-Path $BuildDirectory "_deps/aether-client-cpp-build/generated/aether-catalog"
    $catalog = Join-Path $BuildDirectory "update-lab-package/aether-catalog"
    if (Test-Path -LiteralPath "$generated/build.json") {
        New-Item -ItemType Directory -Path $catalog -Force | Out-Null
        Copy-Item -LiteralPath "$generated/build.json" -Destination $catalog -Force
        Copy-Item -LiteralPath "$generated/catalogs" -Destination $catalog -Recurse -Force
    }
    foreach ($revision in 0..1) {
        if (Test-Path -LiteralPath "$generated/build.json") { $publish += @("--windows-x64-e$revision-telemetry", $catalog) }
        $publish += @("--windows-x64-e$revision-symbols", "$binaries/update_lab_e$revision.pdb")
    }
    if ($LinuxBinaries) {
        $linuxPayload = (Resolve-Path -LiteralPath $LinuxBinaries).ProviderPath
        $publish += @("--linux-x64-launcher", (Join-Path $linuxPayload "update_lab_launcher"))
        foreach ($revision in 0..2) {
            $publish += @("--linux-x64-e$revision", (Join-Path $linuxPayload "update_lab_e$revision"))
            $catalog = Join-Path $linuxPayload "aether-catalog"
            if (Test-Path -LiteralPath "$catalog/build.json") { $publish += @("--linux-x64-e$revision-telemetry", $catalog) }
            $symbols = Join-Path $linuxPayload "update_lab_e$revision.debug"
            if (Test-Path -LiteralPath $symbols) { $publish += @("--linux-x64-e$revision-symbols", $symbols) }
        }
    }
    & "$binaries/update_lab_provider.exe" @publish
    if ($LASTEXITCODE) { throw "Publish fixtures failed" }
} finally { Pop-Location }
