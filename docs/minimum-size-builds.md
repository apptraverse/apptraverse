# Minimum-size 64-bit consumers

Enable `APPTRAVERSE_MINIMUM_64=ON` and build `MinSizeRel`. The profile is
opt-in: ordinary Debug builds retain their producer behavior. Supply headers
must come from the matching schema; consumers do not generate or migrate them.

The profile enables LTO, section elimination, static MSVC CRT and native ELF
stripping. It disables RTTI, C++ exceptions, stack protection, floating-point
traps/errno, telemetry and optional SIMD/autovectorization. SSE2 remains the
mandatory x86-64 scalar ABI baseline. This is an explicit size-oriented profile,
not a recommendation for builds requiring those protections or strict IEEE math.

Native Aether consumers use the pinned SDK with Hydrogen only and TCP. Cloud
registration/DNS remain enabled. Browser consumers use WebSocket, with native
TCP/DNS disabled. Do not use whole-archive linking for the object system: retain
the existing explicit registration entry points and let LTO/section GC remove
unused implementations.

For Windows use a Visual Studio x64 generator and `--config MinSizeRel`.
For Linux use `CMAKE_BUILD_TYPE=MinSizeRel`. Android builds in this acceptance
use only `x86_64`; pass the profile to the parent JNI CMake directory before
adding AppTraverse so it also covers the final JNI target.

Web acceptance uses Emscripten 7.0.0 with `-sMEMORY64=1` on both C and C++
compilation. A 32-bit Wasm module does not satisfy this profile. The native
Emval handle remains opaque in C++; JS handles are i32 indices, while actual
wasm64 pointers passed to exported functions are BigInt. Use
`APPTRAVERSE_EM_JS_HANDLE` for JS functions returning Emval handles.

SharedNode Chat has Windows, Linux and Apple source targets. Android/Web
presenters are not implemented; requesting them fails explicitly. Apple was
not built or executed in the Windows-host acceptance. MinimalApp and
WindowCatalog have Windows/Linux/Android/Web acceptance targets. Do not confuse
framework portability with an application's independently implemented ports.
