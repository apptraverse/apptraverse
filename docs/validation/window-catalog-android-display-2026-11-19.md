# Android WindowCatalog display port — 2026-11-19

Scope: existing `Aether_NDK_Smoke_x86_64`, Android 14/API34,
`emulator-5554`, x86_64, 1080×2400 at 420dpi. JDK20, SDK34,
NDK29.0.14206865 and cached CMake4.1.2/v14314.44 Debug builds were reused
offline. The connected physical device and other applications were not changed.
The emulator Catalog package was initially absent with retained application data;
reinstalling the retained old APK exposed those historical profiles. All writes
for this acceptance use new named profiles, preserving the old profiles.

## Model and native boundary

Android authors the shared paged display menu and typed `CatalogUiState` LOCAL
root. The native menu offers Enter/Exit fullscreen for the current page and
Automatic/Horizontal/Vertical/Reset layout choices only on the catalog page.
Labels and selected state come from the published native UI mirror. A fullscreen
popup captures its page identity and explicit desired state, so a queued command
cannot toggle another page because its caption became stale. Back resolves the
current published page. Both paths invoke `RequestWindowFullscreenFromUi` through
the model proxy; fullscreen preference changes are ordinary LOCAL Events.

The Activity projects the current page's published preference onto Android
immersive system bars. Its native ActionBar and overflow remain accessible for
touch and accessibility Exit commands. Android's transient system-bar swipe and
display-cutout safe area remain OS-owned. Focus, Activity remount and multi-window
callbacks reapply the preference. API30+ uses WindowInsetsController; API24–29
uses immersive-sticky flags. API33+ registers a fullscreen Back callback and
unregisters it on exit/destroy; earlier systems use `onBackPressed`. Windowed Back
retains ordinary Android behavior. The OS can retain bars in unsupported window
modes without rewriting LOCAL intent. No desktop monitor/window positioning is
attempted for the single Activity.

Existing native configuration, density, text scaling, accessibility and display
observations remain in the common LOCAL environment flow. Startup prepares that
baseline before publication and enables shared active-window filtering while
preserving dormant LOCAL window records. USER/LOCAL use separate ordinary RAM
stores and persist at onStop. A native indeterminate ProgressBar replaces the
fixed English loading label; there is no second LOCAL load for shell language.
Catalog registration uses the native-menu model registrar and Android window,
button and list registrar. The aggregate Android registration still registers
text controls for other consumers.

## Executed native acceptance

Final `instrument-accepted-eleven.log`: **OK (11 tests), 172.715 seconds**.
This is the nine previously accepted native cases plus both new display cases,
with the fullscreen case additionally replacing/remounting its Activity while
fullscreen. The nine retained scopes cover empty-ring swipe, Add/navigation/
Close/reopen/explicit Open/Delete, native background cold restore, RU/EN
propagation, rotation/Activity remount, live native SP enlargement, all native
appearance/LOCAL cold-restore assertions and disabled native text-state feedback.
The two historical Recents-dismiss cases were excluded and remain NOT PASS.

Normal fresh Debug's targeted native cold-policy case also passed:
`instrument-debug-fresh.log`, **OK (1 test), 8.061 seconds**. It writes native
USER/LOCAL at onStop, cold-starts a new empty graph, and preserves the previous
USER bytes/timestamp. This is a narrow fresh-Debug runtime check, not a claim
that all eleven cases ran under both build policies.

`instrument-new-settled.log`: **OK (2 tests), 53.763 seconds**.

- Fullscreen follows independent main/child preferences through page navigation,
  rotation, Back exit, onStop, process termination, cold LOCAL restore and child
  Close/reopen cleanup. Commands remain RAM-backed before onStop.
- Native layout choices project published selected states, Reset returns to the
  supply default, child pages omit main-layout commands, and RU menu labels come
  from the same model locale.

The first diagnostic run encountered Android's one-time immersive education
overlay. The test now dismisses that native overlay when present. A later test
needed to await actual overflow disappearance before injecting another Back;
that UI barrier is now explicit. Earlier failed logs remain diagnostic evidence
and are not counted as PASS.

The old language-menu test had one direct Menu-key injection after submenu Back
that bypassed its existing input-focus precondition. An external native
UiAutomator probe confirmed normal submenu dismissal and the correct subsequent
RU menu (`native-language-after-back.xml` and
`native-language-after-menu-key.xml`). The test now waits for that submenu to
disappear and for the real Activity input focus before opening the menu. Its
isolated rerun passed **OK (1 test), 16.755 seconds**.

Native screenshots `immersive-main.png` and `immersive-child-three.png` show
hidden status/navigation bars with the native ActionBar retained.
`immersive-window.txt` records the system's requested hidden status/navigation
types and transient-by-swipe behavior.

## Actual compatible APK update and USER-only imports

The retained old APK loaded a retained Android-native three-window USER/LOCAL
pair into `port-old-three-20261119` and saved it at its ordinary onStop boundary.
Actual `adb install -r` replaced that old APK with the new DebugLoadOnly APK.
Cold launch retained three holder/window identities, RU, Dark/soft, Android OS
baseline and native presentation. New fullscreen commands executed on this
existing LOCAL profile without migration or reset.

The resulting native Android LOCAL contains fullscreen main2 and
child3(2520653970). Independent new profiles receive Windows USER with two or
four holders and exactly the same Android LOCAL bytes; a supplied missing seed
path verifies that existing LOCAL wins without reading the seed. Both initial
native launch/onStop and subsequent cold launch passed:

- USER holder count becomes2 or4 with matching original identities/open states.
- RU, Dark/soft, OS revision1, display baseline, all four previous window
  observations and placements remain unchanged.
- Fullscreen IDs2 and2520653970 remain in LOCAL in both cases. Child3 is dormant
  under the smaller USER and remains available under the larger USER.
- USER4 adds its fourth window's Android observation without replacing the
  existing LOCAL records. Target-ABI inspection rejects LOCAL roots/layers/Events
  leaking into portable USER.

Evidence: `outputs/android-port-20261119/native-transfer-results.json`,
`native-transfer.log`, preserved preport/current profiles and native screenshots.
The target-ABI environment fixture now prints effective layout flow, fullscreen
IDs and placement records directly.

## Boundaries

The historical native Recents-dismiss failures remain outside these PASS claims;
this port does not reclassify them. Android API24–32 execution, physical devices,
ARM64, split/freeform/PiP, actual OS-image upgrades and Release packaging remain
NOT RUN. The shared four-platform fixture matrix uses isolated generated scratch
profiles; its Android transport now carries optional `local/user.bin` like the
other runners and checks unchanged USER and LOCAL hashes on host and device.
This fixture correction does not import LOCAL in production USER-only transfers.

## Final artifacts

Cached incremental DebugLoadOnly/test packaging passed, as did normal Debug/test
packaging (1m59s for the first updated fresh build, subsequent incremental test
packaging1–3s). No clean build or Release was used. APKs and the machine-readable
manifest are retained in `outputs/android-port-20261119`:

| APK | SHA256 |
| --- | --- |
| `window-catalog-debugloadonly.apk` | `6f4469310f06c32138c4819ac521e41653fff2d4d139b2a283faf0c03aac4044` |
| `window-catalog-debug-fresh.apk` | `fb377983bfd9cb85f8518474297d258a69f1d18a0cf9456f77e118a602004bb6` |

The emulator ends with the current DebugLoadOnly APK and its matching test APK
installed. A dedicated three-window RU/Dark-soft native profile is open on its
fullscreen catalog page (`final-current-loadonly-three-main.png`). Historical
profiles remain intact. The physical device was not used.

## Verification after shared-builder integration

After integration of the concurrent Apple/shared-builder changes, cached
DebugLoadOnly packaging passed (1m2s), then the toolbar-authoring cleanup was
consumed by a second incremental build (15s). The actual native compile include
path selects
`examples/window_catalog/android/app/.cxx/Debug/675615d2/x86_64/generated/embedded_distilled_window_catalog_android_supply.h`.
Its bytes remain identical to the preserved pre-integration header:
SHA256 `af1cb141cdaa8ce36c6ddce5f5b955045d68d56139ba0e21ab7f7dc42145b66f`.
Compile commands and byte comparison are recorded in
`integrated-native-compile-commands.txt` and `integrated-acceptance.json`.

The first two focused native invocations reproduced a test-route failure: after
returning from landscape to portrait, its swipe ran before native orientation
and viewport geometry settled. The input trace included negative-coordinate
edge-swipe events and delayed processing; these failed runs are retained as
diagnostics, not PASS. An external native check waited for the actual UI
hierarchy rotation and Close-button viewport geometry in each orientation.
Landscape Close was `[160,923][2369,1049]`, portrait Close was
`[32,2243][1049,2369]`, and the subsequent native swipe reached Window Catalog
with Window1. See `integrated-external-rotation.log` and its native screenshots.

The narrowly scoped test correction now requires the requested native rotation
and the existing Close-button viewport constraints before post-rotation input.
It uses bounded observation polling; there is no fixed delay or product change.
With that barrier, the integrated APK's focused display cases passed:
`instrument-integrated-viewport-display.log`, **OK (2 tests), 63.433 seconds**.
This is a focused post-integration check; the earlier eleven-case and fresh-Debug
results retain their recorded baseline scope.

The current integrated artifact is `window-catalog-debugloadonly-integrated.apk`,
SHA256 `9fc74f5140f0a231ffdd9caaceff79c55718751d19e51e4dd5807ea81cc649f0`.
The emulator again ends with this current load-only APK and matching test APK,
with the same dedicated three-window RU/Dark-soft profile open on its fullscreen
catalog page (`final-integrated-loadonly-three-main.png`). Original free
user-rotation mode was restored after the external probe; the physical device
remained untouched.
