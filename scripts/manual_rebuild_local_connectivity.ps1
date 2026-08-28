$ErrorActionPreference = 'Stop'
$root = 'C:\Users\nickc\Projects\AppTraverse-mcp'
$build = Join-Path $root 'build\win64-ninja-msvc-release'
$pin = Join-Path $root '.artifacts\aether-pin-941744cd'

$common = @(
  '/nologo', '/c',
  '/DWIN32', '/D_WINDOWS', '/EHsc', '/O2', '/Ob2', '/DNDEBUG',
  '/std:c++20', '/MD', '/wd4702', '/wd4996', '/Zc:preprocessor',
  '/Os', '/DNDEBUG', '/Zc:__cplusplus', '/Zc:externConstexpr', '/bigobj',
  '/DAE_DISTILLATION=1', '/DAE_FILTRATION=1', '/DCARES_STATICLIB',
  '/DNOMINMAX', '/DSODIUM_STATIC', '/DSTDEXEC_ENABLE_WINDOWS_THREAD_POOL',
  '-DUSER_CONFIG=\"C:/Users/nickc/Projects/AppTraverse-mcp/cmake/aether_user_config.h\"',
  '/DWIN32_LEAN_AND_MEAN',
  "/I$pin",
  "/I$build\_deps\aether-tele-src\src",
  "/I$build\_deps\aether-miscpp-src\src",
  "/I$build\_deps\numeric-src",
  "/I$build\_deps\gcem-src\include",
  "/I$build\_deps\libbcrypt-src",
  "/I$build\_deps\libsodium-src\src\libsodium\include",
  "/I$build\_deps\libhydrogen-src",
  "/I$build\_deps\c-ares-build",
  "/I$build\_deps\c-ares-src",
  "/I$build\_deps\c-ares-src\include",
  "/I$build\_deps\etl-src\include",
  "/I$build\_deps\stdexec-src\include",
  "/I$build\_deps\stdexec-build\include"
)

Push-Location $build
try {
  & cl @common `
    "/FoCMakeFiles\aether.dir\.artifacts\aether-pin-941744cd\aether\\" `
    "$pin\aether\client_connectivity_policy.cpp"
  if ($LASTEXITCODE -ne 0) { throw 'client_connectivity_policy compile failed' }

  & cl @common `
    "/FoCMakeFiles\aether.dir\.artifacts\aether-pin-941744cd\aether\cloud_connections\\" `
    "$pin\aether\cloud_connections\ping_cloud_servers.cpp"
  if ($LASTEXITCODE -ne 0) { throw 'ping_cloud_servers compile failed' }

  Copy-Item aether.lib aether.lib.bak -Force
  lib /nologo /OUT:aether.lib `
    CMakeFiles\aether.dir\.artifacts\aether-pin-941744cd\aether\client_connectivity_policy.cpp.obj `
    CMakeFiles\aether.dir\.artifacts\aether-pin-941744cd\aether\cloud_connections\ping_cloud_servers.cpp.obj `
    aether.lib.bak
  if ($LASTEXITCODE -ne 0) { throw 'lib merge failed' }

  $demoCommon = @(
    '/nologo', '/c',
    '/DWIN32', '/D_WINDOWS', '/EHsc', '/O2', '/Ob2', '/DNDEBUG',
    '/std:c++20', '/MD', '/utf-8', '/Zc:preprocessor',
    '/Os', '/DNDEBUG', '/Zc:__cplusplus', '/Zc:externConstexpr', '/bigobj',
    '/DAE_DISTILLATION=1', '/DAE_FILTRATION=1', '/DNOMINMAX', '/DSODIUM_STATIC',
    '/DSTDEXEC_ENABLE_WINDOWS_THREAD_POOL',
    '-DUSER_CONFIG=\"C:/Users/nickc/Projects/AppTraverse-mcp/cmake/aether_user_config.h\"',
    '/DWIN32_LEAN_AND_MEAN',
    "/I$root\examples\chat_ui_runtime_demo\windows",
    "/I$root\examples\chat_ui_runtime_demo\common",
    "/I$root\include",
    "/I$pin",
    "/I$build\_deps\aether-tele-src\src",
    "/I$build\_deps\aether-miscpp-src\src",
    "/I$build\_deps\numeric-src",
    "/I$build\_deps\gcem-src\include",
    "/I$build\_deps\libbcrypt-src",
    "/I$build\_deps\libsodium-src\src\libsodium\include",
    "/I$build\_deps\libhydrogen-src",
    "/I$build\_deps\etl-src\include",
    "/I$build\_deps\stdexec-src\include",
    "/I$build\_deps\stdexec-build\include"
  )
  & cl @demoCommon `
    '/Foexamples\chat_ui_runtime_demo\windows\CMakeFiles\win32_chat_ui_runtime_demo.dir\aether_runtime.cpp.obj' `
    "$root\examples\chat_ui_runtime_demo\windows\aether_runtime.cpp"
  if ($LASTEXITCODE -ne 0) { throw 'aether_runtime compile failed' }

  & link /nologo /INCREMENTAL:NO /subsystem:windows /ENTRY:mainCRTStartup `
    '/OUT:examples\chat_ui_runtime_demo\windows\win32_chat_ui_runtime_demo.exe' `
    examples\chat_ui_runtime_demo\windows\CMakeFiles\win32_chat_ui_runtime_demo.dir\main.cpp.obj `
    examples\chat_ui_runtime_demo\windows\CMakeFiles\win32_chat_ui_runtime_demo.dir\win_app.cpp.obj `
    examples\chat_ui_runtime_demo\windows\CMakeFiles\win32_chat_ui_runtime_demo.dir\win_presenters.cpp.obj `
    examples\chat_ui_runtime_demo\windows\CMakeFiles\win32_chat_ui_runtime_demo.dir\aether_runtime.cpp.obj `
    examples\chat_ui_runtime_demo\chat_ui_runtime_demo_common.lib apptraverse.lib aether.lib `
    _deps\aether-tele-build\aether-tele.lib _deps\libbcrypt-build\bcrypt.lib `
    _deps\libsodium-build\sodium.lib _deps\libhydrogen-build\hydrogen.lib `
    _deps\c-ares-build\src\lib\cares.lib user32.lib gdi32.lib advapi32.lib iphlpapi.lib ws2_32.lib
  if ($LASTEXITCODE -ne 0) { throw 'link failed' }

  Write-Host 'manual rebuild ok'
}
finally {
  Pop-Location
}
