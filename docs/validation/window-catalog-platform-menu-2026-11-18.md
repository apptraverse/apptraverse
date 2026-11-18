# WindowCatalog platform menu validation — 2026-11-18

The Linux and Android menus use the same distilled commands as Windows. Each
platform presents its own native controls and stores language and theme
in LOCAL. Swift AppKit and SwiftUI menus on macOS and iOS expose Language and
follow system appearance. Apple build, device and simulator verification were
left for a separate task at the user's request.

## Debug and transfer

- GTK on WSL with private X11/Openbox. Client mount, local environment,
  menu/Add and native lifecycle acceptance passed. The settings test now checks
  the native minimum width of the larger menu after a system font change.
- Windows Debug built the producer, embedded consumer and portable USER
  inspector. The menu model and native chrome tests passed. The full Win32
  acceptance rerun stopped at the foreground-window precondition in this
  desktop session.
- Android API 34 x86-64 emulator. The Debug and DebugLoadOnly APKs and the
  DebugLoadOnly instrumentation APK built against the current distilled supply.
  Three focused instrumentation tests passed in 352.374 seconds. They cover
  all four theme choices and cold restore, language on the catalog and a live
  child, and LOCAL language restore with native radio check state.
- Windows → Android with no original LOCAL restored the open window and its
  active identity. LOCAL began with English and system theme. Import into an
  existing Android profile retained English, Light and its OS snapshot.
- Android → Windows restored a closed window with its original holder and
  window identities and next-window number, without importing Russian.
  Windows retained English, system theme, and its own monitor/revision.
- The copy operation replaced only `user.bin`. SHA-256 of the destination's
  existing `local/user.bin` was identical immediately before and after copying
  in both directions. After cold start, normal native observations added a
  mount for the imported Android window. The Android inspector rejects any
  LOCAL object, layer or event found in portable USER.

The transfer used actual lifecycle output and embedded DebugLoadOnly
consumers from matching S0 supplies. It did not seed LOCAL or use a converter.

## Minimum size

Bytes on disk. Android lists the native x86-64 library **inside** the installed
APK separately from the complete APK. Debug and Linux/Windows load-only
executables can differ from the Debug producer because the producer distills
its initial graph and the consumer embeds the distilled graph.

| Artifact | Debug | Debug load-only | MinSizeRel |
| --- | ---: | ---: | ---: |
| Windows x64 EXE | 5,488,640 | 5,162,496 | 873,984 |
| Linux x86-64 ELF | 43,314,832 | 38,736,064 | 688,504 |
| Android x86-64 native `.so` | 8,293,584 | 8,292,592 | 1,922,656 |
| Android x86-64 APK | 15,998,746 | 15,997,754 | 1,945,933 |

Windows MinSizeRel started from saved USER/LOCAL and restored the existing
window and Russian title in two native launches. Linux's ELF was stripped.
The MinSizeRel native GTK acceptance passed, and the actual executable
accepted Language by keyboard and saved both state files. Both desktop
MinSizeRel load-only executables had the same size as the corresponding
normal MinSizeRel executable.

The Android MinSizeRel APK with R8 installed and started on the emulator. The
actual native menu selected Light and Russian. Add and Close persisted a closed
child. Home followed by process stop and cold launch restored the Russian
catalog, closed child, and checked Light option in the Theme submenu.

Local logs are in `.artifacts/windowcatalog-platform-port`. The passing
transfer, Android instrumentation and release logs are `transfer.log`,
`android-debug-instrument.log`, and `android-minsize-test.log`. Desktop release
evidence is in `linux-minsize-test3.log` and `windows-minsize-test.log`.

Apple binary sizes were not measured. Linux was tested on X11. Android
packaging and tests use only the x86-64 emulator ABI.
