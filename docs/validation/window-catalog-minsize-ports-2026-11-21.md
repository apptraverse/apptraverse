# WindowCatalog MinSizeRel on Linux, headless, Web and Android

The Linux and Web consumers use their matching Debug-generated supply.
WindowCatalog's Android MinSizeRel build uses the same scoped size profile
as the other native consumers, with its own Debug-generated supply.
The profile and run commands are documented in
[Minimum-size consumers](../minimum-size-builds.md).

## Canon boundaries

- Rules 1–9. The platform-specific changes leave the shared layout model,
  S0 identities, embedded supply format, USER and LOCAL storage, and lazy
  loading on the existing paths.
- Rules 10–18. The GTK conversion of a requested viewport size to a native
  window size measures chrome at the destination width. The presenter still
  reports native observations to the model. Android preserves the name of the
  fullscreen callback used by JNI to apply the publication.
- Rules 19–27. Headless exercises the common WindowCatalog model. The
  portable CLI checks the loaded graph with the display unset. Web and
  Android retain their native hosts. Android WindowCatalog excludes text
  controls from its JNI host ABI. Localization and settings stay in the
  existing supply and publication contracts.
- Rules 28–30. The MinSizeRel checks inspect the actual ELF/Wasm artifact and
  live link map. The headless model test intentionally builds its graph.
  The GTK, Web and Android consumers and the headless portable CLI check
  load-only linkage, including the absence of graph builders, SharedNode
  and the network SDK.

## Executed checks

| Target | Artifact bytes | Evidence |
| --- | ---: | --- |
| Linux GTK x86_64 | 1,357,224 | Debug 3/3 and MinSizeRel 4/4 in private Xvfb/Openbox. Tests covered menu/Add, native resize and fullscreen, LOCAL cold restore, client mount and lifecycle. |
| Linux headless x86_64 | 1,491,144 model test; 1,348,872 portable CLI | Model test passed. Separate portable CLI processes seeded and inspected USER and LOCAL with both display environment variables unset. |
| Web wasm32 | 527,715 Wasm; 140,038 JS; 10,635 HTML; 299 worker | Embedded session passed in Node. Chrome passed layout/localization, fullscreen, keyboard handling, unchanged USER, cold LOCAL restore, and Close/reopen/Delete. |
| Android x86_64, API 34 emulator | 1,896,941 APK; 1,873,424 packaged JNI ELF | DebugLoadOnly instrumentation passed 4/4, including fullscreen/rotation and cold LOCAL restore. MinSizeRel handled native Add, onStop save, cold child restore, fullscreen and Back. |

All three Linux ELF contracts, the Web Wasm contract and both Android ELF
checks (native output and the library extracted from the APK) passed. The pure
headless model test imports only the ELF loader, C and math runtimes. The
portable CLI links the Linux factory set and ran with no display.
The link-inspector policy suite passed 11 cases.

WSLg Wayland passed the focused fullscreen and cold restore scenario in
Debug and in repeated MinSizeRel runs. The MinSizeRel probe was intermittent.
Other runs timed out waiting for fullscreen exit or the requested child
viewport, and one recorded a changed saved normal frame. The X11 acceptance
passed with the exact frame and viewport assertions enabled.

The initial Android APK exited when JNI looked up the fullscreen callback
after R8 renamed it. The MinSizeRel keep rule retains the exact signature.
The Android link map also identified unused text controls. The narrow
WindowCatalog variant excludes their C++ bridge methods, factory and native
presenter. The packaged JNI ELF imports only `libc`, `libdl`, `liblog` and
`libm` and is the APK's sole native library. The independent Android
MinimalApp Debug build passed with the text controls enabled.
