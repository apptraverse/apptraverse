param(
    [string]$BuildDirectory = "build/win-update-lab",
    [string]$SdkSource = "",
    [string]$ObjectsSource = "",
    [string]$TelemetrySource = "",
    [string]$LinuxBinaries = "",
    [string]$Cache = "C:/cpm"
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
    $binaries = (Resolve-Path "$BuildDirectory/examples/update_lab/Debug").Path
    $publish = @('publish', '--out', 'artifacts/update-lab/store',
        '--windows-x64-e0', "$binaries/update_lab_e0.exe", '--windows-x64-e1', "$binaries/update_lab_e1.exe", '--windows-x64-e2', "$binaries/update_lab_e2.exe")
    if ($LinuxBinaries) {
        $linuxPayload = (Resolve-Path -LiteralPath $LinuxBinaries).ProviderPath
        foreach ($revision in 0..2) {
            $publish += @("--linux-x64-e$revision", (Join-Path $linuxPayload "update_lab_e$revision"))
        }
    }
    & "$binaries/update_lab_pack.exe" @publish
    if ($LASTEXITCODE) { throw "Publish fixtures failed" }
} finally { Pop-Location }
