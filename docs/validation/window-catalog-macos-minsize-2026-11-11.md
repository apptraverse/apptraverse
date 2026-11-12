# macOS MinSizeRel size optimization — 2026-11-11

The existing iOS size policy is shared through `cmake/apple_min_size.cmake`.
Independent `APPTRAVERSE_MACOS_MIN_SIZE_OPTIMIZATIONS` and iOS options default
ON and apply only to their native MinSizeRel configuration. C-family uses -Oz,
ThinLTO, hidden symbols, function/data sections; C++ has no exceptions/RTTI,
Objective-C exceptions are disabled. Linker dead stripping and product strip -x
are enabled. Swift keeps -Osize/WMO and the platform ABI. LLVM archive tools
match the active Clang, including MacPorts LLVM20. No model, presenter, backend
library implementation, USER schema or lifecycle changes.

## Executed validation

- Intel macOS13.7.8, Xcode15.2/Swift5.9.2, MacPorts Clang20.1.8, x86_64.
- Built macos_window_catalog_demo successfully, 74 steps, followed by final
  incremental relink after configuration cleanup.
- Debug-distilled current supply paired with fresh isolated mac-size-opt-profile.
  Previous application bundles and user profiles preserved.
- Final stripped product started as PID76854; native window visible and launch
  complete. Orderly native termination exited0 and wrote exactly one user.bin.
- Cold restart from that profile started as PID76875 with a visible native window.
  Left running for manual review. This is a startup/save/restart smoke; complete
  native click acceptance and ARM64/device coverage were not rerun here.
- iOS MinSizeRel configuration regenerated successfully after helper rename;
  no iOS runtime retest in this macOS-only task.

## Size

Executable: 543,464 bytes (530.7KiB). Sum of app bundle file sizes: 544,474 bytes
(531.7KiB). Historical macOS MinSizeRel executable was 1,199,752 bytes; the new
artifact is about54.7% smaller. That historical artifact used an older supply,
so this is not a controlled same-revision compiler-flag comparison.

Artifact: outputs/WindowCatalog macOS MinSizeRel.app, outside the source tree.
Profile: work/mac-size-opt-profile. Build and native smoke logs are retained in
work/mac-size-opt-*. The delivered bundle contains no USER profile.
