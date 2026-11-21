# Minimum-size consumers

Enable `APPTRAVERSE_MINIMUM_64=ON` and build `MinSizeRel`. The profile is
opt-in: ordinary Debug builds retain their producer behavior. Supply headers
must come from the matching schema; consumers do not generate or migrate them.

The profile enables LTO, section elimination, static MSVC CRT, static Linux
C++ runtime and native ELF stripping. Android WindowCatalog uses the NDK's
static C++ runtime. The profile disables RTTI, C++ exceptions, stack
protection, floating-point traps/errno, telemetry and optional
SIMD/autovectorization. SSE2 remains the
mandatory x86-64 scalar ABI baseline. This is an explicit size-oriented profile,
not a recommendation for builds requiring those protections or strict IEEE math.

## Windows x86 WindowCatalog

`APPTRAVERSE_MINIMUM_WINDOWS_X86=ON` reproduces the measured x86 MinSizeRel
recipe without setting or replacing `CL`, `_CL_`, `LINK`, `_LINK_`, or CMake's
global flag strings. Use an x86 VS 2022 developer environment with desktop
v143 (14.44); start without inherited compiler/linker flag overrides. The option
rejects other architectures and cannot be combined with `APPTRAVERSE_MINIMUM_64`.

The added MinSizeRel compile options are `/O1 /Ob2 /Oi /Os /Oy /GL /Gy /Gw /GF
/GS- /d2Qvec- /fp:fast /fp:except- /Zc:inline /Zc:checkGwOdr`, with
`AE_TELE_ENABLED=0`. C++ also uses `/GR- /EHs-c-` and `_HAS_EXCEPTIONS=0`.
Link options are `/LTCG /OPT:REF /OPT:ICF=10 /INCREMENTAL:NO /GUARD:NO`.
They cover dependencies and AppTraverse targets; the interface compile policy
keeps the same inline, floating-point and ICF choices instead of overriding
them later. Release configurations retain static `/MT`; Windows 7 remains the
minimum. `/GS-` and `/GUARD:NO` are deliberate size choices, and `/fp:fast`
permits relaxed floating-point semantics. `/d2Qvec-` is toolset-specific, so the
recipe is tied to the measured v143 toolset. The prior measured executable was
607,744 bytes; a new toolchain/source/supply requires a new measurement.

Run from the repository root in that x86 developer PowerShell. Prepare both
matching supplies with the existing Debug `*_load_only` targets: they build
the ordinary Debug producers and invoke their `--distill-only` modes. The root
consumer configure needs both headers even when only WindowCatalog is built.
Reuse an existing matching x86 Debug build and its headers when available;
reuse the existing CPM dependency cache with `-DCPM_SOURCE_CACHE=...` when needed.
The optional `APPTRAVERSE_BUILD_EVOLUTION_TESTS=OFF` keeps this build focused.

```powershell
cmake -S . -B build/win32-ninja-msvc-debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
  -DAPPTRAVERSE_MINIMUM_WINDOWS_X86=OFF -DAPPTRAVERSE_MINIMUM_64=OFF `
  -DAPPTRAVERSE_BUILD_AETHER_LINK=OFF -DAPPTRAVERSE_BUILD_SHARED_NODE_CHAT=OFF `
  -DAPPTRAVERSE_BUILD_EVOLUTION_TESTS=OFF
cmake --build build/win32-ninja-msvc-debug --target `
  win32_window_catalog_demo_load_only win32_minimal_app_load_only
$catalogSupply = (Resolve-Path "build/win32-ninja-msvc-debug/examples/window_catalog/generated_distilled/embedded_distilled_window_catalog_supply.h").Path
$minimalSupply = (Resolve-Path "build/win32-ninja-msvc-debug/examples/minimal_app/windows/generated_distilled/embedded_distilled_minimal_app_supply.h").Path
cmake -S . -B build/win32-ninja-msvc-minimum -G Ninja `
  -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
  -DAPPTRAVERSE_MINIMUM_WINDOWS_X86=ON -DAPPTRAVERSE_MINIMUM_64=OFF `
  -DAPPTRAVERSE_BUILD_AETHER_LINK=OFF -DAPPTRAVERSE_BUILD_SHARED_NODE_CHAT=OFF `
  -DAE_NO_DEBUG_LOG=ON -DAE_OBJECTS_RAW_TELE=OFF `
  -DAPPTRAVERSE_BUILD_EVOLUTION_TESTS=OFF -DBUILD_TESTING=ON `
  "-DAPPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER=$catalogSupply" `
  "-DAPPTRAVERSE_MINIMAL_APP_DEBUG_SUPPLY_HEADER=$minimalSupply"
cmake --build build/win32-ninja-msvc-minimum --target win32_window_catalog_demo
ctest --test-dir build/win32-ninja-msvc-minimum -C MinSizeRel --output-on-failure `
  -R '^(apptraverse_windows_binary_contract_policy_test|win32_window_catalog_demo_binary_contract)$'
```

The contract reads the generated PE32 executable and eight-digit-address map,
checks static runtime linkage, portable factories and excluded distillation,
SharedNode and network SDK libraries/objects/symbols. It also supports PE32+
and sixteen-digit-address maps for existing x64 consumers. The object-system
library `aether-objects` is permitted. This is binary evidence; native GUI and
actual Windows 7 execution are separate checks. Debug remains the ordinary
fresh-graph producer, and option-OFF/non-x86 flags retain their existing policy.

## Linux, headless, Web and Android WindowCatalog

For Linux and Web, configure a separate Ninja MinSizeRel build with
`APPTRAVERSE_MINIMUM_64=ON`, `APPTRAVERSE_BUILD_AETHER_LINK=OFF` and
`APPTRAVERSE_BUILD_SHARED_NODE_CHAT=OFF`. Use the matching WindowCatalog
Debug supply for that backend. The contract targets require Python 3.9 or
later and write a linker map and adjacent JSON report to the build tree.

| Target | Debug supply input |
| --- | --- |
| `linux_window_catalog_demo_binary_contract` | `APPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER`, generated by Debug `apptraverse_window_catalog_embedded_supply` |
| `apptraverse_window_catalog_headless_test_binary_contract` | The in-process model test creates its graph and checks the network exclusion |
| `apptraverse_window_catalog_portable_user_fixture_binary_contract` | With the Linux backend enabled, uses the same Debug supply as the GTK app. Its CLI can seed and inspect USER and LOCAL with no display |
| `web_window_catalog_demo_binary_contract` | `APPTRAVERSE_WINDOW_CATALOG_WEB_DEBUG_SUPPLY_HEADER`, generated by Debug `apptraverse_window_catalog_web_supply` |
| `apptraverse_window_catalog_jni_binary_contract` | Gradle `-Papptraverse.debugSupplyHeader=...`, the header generated by Android Debug host distillation |

Build and verify each target with `cmake --build <build-dir> --target <target>`.
For Android, run Gradle `assembleMinSizeRel` with
`-Papptraverse.abiFilters=x86_64` and the Debug supply input. Then use CMake to
run the contract target in the MinSizeRel x86_64 native build directory
under `app/.cxx`. The standalone inspector can also check the ELF library
extracted from the APK against that same link map.

The load-only contract checks the embedded supply, portable window Events
and absence of graph builders, unused control implementations, SharedNode,
and the Æther network SDK. The model test keeps its graph-building code and
applies the network and linkage checks. The object-system library
`aether-objects` is permitted. These WindowCatalog Web builds use wasm32.

## 64-bit consumers

Native Aether consumers use the pinned SDK with Hydrogen only and TCP. Cloud
registration and system DNS are enabled by default. Set `AE_DNS_BACKEND` in
the SDK user config to `AE_DNS_NONE`, `AE_DNS_CARES`, or `AE_DNS_SYSTEM`.
Windows uses asynchronous Winsock where available and a background worker
on Windows 7. Apple uses DNS-SD and Linux and Android use background
`getaddrinfo`. Browser consumers use WebSocket and delegate DNS to the browser.
Keep whole-archive linking disabled for the object system. Retain
the existing explicit registration entry points and let LTO/section GC remove
unused implementations.

For Windows use a Visual Studio x64 generator and `--config MinSizeRel`.
For Linux use `CMAKE_BUILD_TYPE=MinSizeRel`. Android builds in this acceptance
use only `x86_64`; pass the profile to the parent JNI CMake directory before
adding AppTraverse so it also covers the final JNI target.

The standalone SharedNode chat defaults to `APPTRAVERSE_AETHER_STREAM_HIERARCHY=OFF`.
This disables the SDK's `AE_ENABLE_STREAM_HIERARCHY` and
`AE_SUPPORT_ADAPTIVE_DATAGRAM` options and retains
the chat's direct `P2pStream` on ID 0. Enable the AppTraverse option for builds
that need to attach application Raw streams to SharedNode.

For a Web consumer explicitly targeting wasm64, use Emscripten 7.0.0 with
`-sMEMORY64=1` on both C and C++ compilation. The native Emval handle remains
opaque in C++. JS handles are i32 indices, while actual wasm64 pointers
passed to exported functions are BigInt. Use
`APPTRAVERSE_EM_JS_HANDLE` for JS functions returning Emval handles.

SharedNode Chat has Windows, Linux and Apple source targets. Android/Web
presenters are not implemented; requesting them fails explicitly. Apple was
not built or executed in the Windows-host acceptance. MinimalApp and
WindowCatalog have Windows/Linux/Android/Web acceptance targets. Do not confuse
framework portability with an application's independently implemented ports.
