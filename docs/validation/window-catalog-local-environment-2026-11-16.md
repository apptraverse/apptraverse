# Windows WindowCatalog: client-local environment — 2026-11-16

Branch: `maintenance/rename-window-catalog-example`, based on `6934e89a`.
Toolchain: Visual Studio 2022 v143 14.44, incremental Debug, Ninja,
`WINVER/_WIN32_WINNT=0x0601`. Existing patched dependency sources reused.
Build: `C:/Users/nickc/Documents/Codex/build-window-layout-win7`.

Contract: [ClientLocalEnvironment](../architecture/ClientLocalEnvironment.md).
This supersedes the previous INI appearance slice and runtime-only OS baseline
policy. It does not change the portable layout solver or introduce an OS layout
solver. Ordinary Debug still starts fresh; load-only consumes matching USER and
LOCAL. Prior experimental profiles and the old appearance INI are not migrated,
reset or read.

## Final executed results

Final source build and selected CTest run: **10/10 PASS, 212.70 seconds**.

| Test | Seconds | Verified boundary |
| --- | ---: | --- |
| `apptraverse_ui_constraint_layout_test` | 0.05 | Existing portable solver |
| `apptraverse_ui_device_state_test` | 0.07 | Initialization vs change, old/new snapshots, masks, dedup, enumeration reorder, per-window DPI, replay, cold file load, seed/keep, loss/return/unavailable screens, immediate compaction |
| `apptraverse_ui_layout_state_test` | 0.07 | Existing distilled constraints, overrides and compatible supply updates |
| `apptraverse_window_catalog_headless_test` | 11.27 | WindowCatalog model/mirror/action boundary |
| `apptraverse_window_catalog_user_persistence_boundary_test` | 5.30 | Existing RAM/disk lifecycle plus real catalog partitioning; local fields/class layers/history absent from portable USER, portable-only and seeded cold load |
| `apptraverse_window_catalog_embedded_overlay_test` | 2.47 | Matching embedded supply/USER behavior |
| `apptraverse_minimal_app_embedded_export_test` | 0.22 | Independent consumer of shared Windows backend |
| `apptraverse_window_catalog_model_test` | 165.38 | Existing catalog model regression |
| `apptraverse_window_catalog_win32_acceptance_test` | 21.41 | Real EXE startup/close/restart and native controls, frame restoration, LOCAL startup comparison and seed policy, existing graph/schema negatives |
| `apptraverse_window_catalog_win32_chrome_adaptive_test` | 6.10 | Real HWND layout, OS observation, profile-local appearance across parent/child, startup monitor fallback and duplicate native notifications |

The real-process acceptance saves an ordinary running application's object
stores, changes its saved DPI/topology baseline through UiDeviceState Events,
then starts the load-only EXE. After shutdown it verifies exactly one new
environment observation, reason Startup, DPI/display flags, restored appearance
and LOCAL exclusion from portable USER. A second restart with unchanged real
settings does not add another environment observation.

The seed acceptance copies portable USER to a new profile, passes the source
LOCAL as `--local-seed-dir`, and verifies inherited appearance after the EXE
closes. On the second run it passes an absent seed path: the existing destination
LOCAL is preserved and the source is not opened. This checks the actual CLI and
lifecycle integration, separately from the pure storage helper.

The native startup fallback fixture provides a saved disconnected-monitor key
and changed baseline, then uses the real host's current monitors, DPI and HWND.
It verifies a model monitor-resolution Event, the applied accessible work area,
actual per-window DPI and retained preferred-monitor key. Synthetic snapshots
exercise secondary-screen return and zero-screen cases without changing this
computer's display configuration.

## Issues exposed and resolved

- The previous native acceptance expected one shutdown file. It now verifies
  one native `user.bin` per partition and no writes from a still-running fresh
  Debug session. This is the newly authorized storage contract.
- Reapplying a freshly observed placement caused native size rounding to change
  an adaptive layout at its square boundary. An observed native action no longer
  issues a new placement request; monitor-resolution Events own that revision.
  The first observation of a newly created HWND is not replayed over a live drag.
- Automatic OS repositioning must not overwrite the preferred monitor. Ordinary
  WINDOWPOS messages report facts, while completed user moves and explicit model
  frame requests update the preference. Native fallback application is guarded
  against feeding back as a user move.
- The independent MinimalApp build exposed an existing direct `GetDpiForWindow`
  call unavailable under the Win7 target. It now uses the common dynamically
  resolved helper with the existing system-DPI fallback.

## Binary audit and delivery

`dumpbin /imports` and `/headers` executed on both final Debug binaries:
subsystem **6.01**, no hard imports for GetDpiForWindow/GetDpiForMonitor,
GetSystemMetricsForDpi, SetProcessDpiAwarenessContext, RoInitialize,
RoActivateInstance, WindowsCreateString, CreateFile2 or CopyFile2.
This is a direct PE import audit, not actual Win7 runtime proof.

Delivered binaries and import reports:
`../outputs/WindowCatalog-local-environment/` outside the repository working tree
(sibling of the repository under the task workspace). The load-only EXE is the
manual restart/persistence entry point; use a fresh isolated profile.

## Explicitly not run

Actual Windows 7, physical monitor hotplug/rotation, physical mixed-DPI monitors,
real screenreader automation, OS High Contrast/text slider toggling in this run,
and new Linux/Android/Web/Apple runtime/USER matrices. The common model schema is
updated; those ports need matching regenerated supplies, as required by the
existing S0/new-profile policy. Historical cross-platform results are not claimed
again by these Windows tests.
