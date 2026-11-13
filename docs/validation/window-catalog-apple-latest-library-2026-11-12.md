# Apple WindowCatalog with latest library — 2026-11-12

## Source and toolchain

Integrated upstream `feature/shared-node-native-memory` at
`8cdb809067c8c531079e609fb1bae84f6b6ee22f` into isolated
`feature/apple-latest-library`, preserving Apple base `3cb608a` (continuing iOS
session/live USER save and independent Apple MinSizeRel policies). Upstream
`main` and the previous Apple branch had no newer commits when fetched.
Aether telemetry is pinned to `64183232c1c6d8ad6119f75ff7e833f43467de57`;
Objects SHA and AppTraverse patch chain are unchanged.

Intel macOS13.7.8, Xcode15.2/Swift5.9.2, MacPorts Clang20.1.8, Ninja/CMake.
Incremental Debug builds use existing build/cache directories. Mobile target is
iOS17.2 Simulator x86_64 on the installed iPhone15Pro simulator. Physical Apple
ARM64/device and other operating systems are outside this run.

## macOS executed

`scripts/build_macos_window_catalog.sh` built both product variants, native
acceptance variants, lifecycle and named common regression targets. Eight
CTest cases passed initially. Native acceptance initially timed out on mouse
selection with `NSApp.isActive=false` and no key/main window; all prior model
operations had passed. No product or test-code workaround was added.

A native-only retry under `caffeinate -d -i -u -t 180` passed in64.29sec:
Debug seed, embedded seed and embedded cold processes;51 PASS checkpoints.
This includes actual NSEvent mouse-down/up routing at the trailing row edge,
double-click child focus, Close/Open/Delete, localization, geometry, lazy row
identity, matching USER cold load, teardown and one USER per seed profile.
Thus all9 named macOS CTests have successful executed results; initial failure
and foreground retry are retained as separate evidence.

Evidence: `work/latest-apple-macos-build-tests.log`,
`work/latest-apple-macos-native-retry.log` and
`work/build-macos/macos-acceptance/20261112-183922-07fbc7dc`.

The final load-only executable was packaged in an isolated ad-hoc-signed
`WindowCatalog macOS Latest.app` using the built target and Debug bundle shell.
Native product smoke passed: PID95901, visible window, orderly native Quit,
exit0 and one3994-byte USER, followed by cold PID95935 and a visible window.
It uses `work/latest-apple-macos-profile`; earlier bundles/profiles are preserved.
Evidence: `work/latest-apple-macos-product.log` and launch status JSON.

## iOS executed

Host Debug exporter built67 steps; simulator products and acceptance apps built
58 steps. Both normal Debug and load-only variants built successfully. Supply
SHA256 is identical on both Apple ports and matches the previous working supply:
`1c884717301fd3d79155701ed69913448b863d6f412cbfe3728f2dc88b7bc3a4`.

All5 acceptance processes passed with the same new binaries: Debug seed23,
embedded seed23, cold5, actual OS lifecycle seed and OS cold5. The OS run
backgrounded into Settings, saved one USER, resumed PID97156 with the same
bridge/model/presenters, accepted later navigation, saved it on a second
background and restored it in a cold process. The application model/USER schema
and Apple lifecycle code required no edits for this upstream integration.

Infrastructure failures are retained separately: initial embedded launch hung
before app output for300sec, and simulator launchctl queries also stalled.
Regular simctl shutdown stalled; SIGTERM to the exact verified per-device
CoreSimulator service completed shutdown, preserving device data. Bootstatus
printed Data Migration Failed while returning0; an actual Settings launch
succeeded. A second run passed Debug but SpringBoard rejected the test-only
`.acceptance.loadonly` as Busy/installing. Uninstall/reinstall of ONLY that
fixture package cleared the stale transaction. After settling, remaining4
processes passed, preserving the same USER between each seed/cold pair.
No product container was erased; no test assertions or application code changed.

Evidence: `work/latest-apple-ios-build.log`, original/retry test logs,
`work/latest-apple-ios-acceptance-retry/20261112-191534` (Debug PASS), and
`work/latest-apple-ios-acceptance-reinstalled/20261112-192059` (remaining4).
The continuation driver imports the canonical runner and executes its same
launch/JSON checks and OS lifecycle helper without reinstalling between phases.

## Additional core checks

All70 build steps compiled and linked. Existing7-case offline target initially
passed6; shared_three_peer_test hit its60sec configured deadline. A standalone
CTest retry used the exact same executable/assertions with180sec and passed
in31.30sec (below even the original limit); no assertions were disabled.
The3 additional CTests (UI materialization, releasing edge, application UI
session) passed3/3 in12.48sec. Thus all10 distinct extra core checks have
successful results, with the original timeout recorded separately.

Ninja emitted premature EOF recovery on each incremental invocation. A final
read-only dry run reproduced stale stored dependency mtimes and planned another
70 steps. This is a build-cache/toolchain limitation, not an application change;
no cache/source cleanup workaround was added. Build logs remain authoritative.
Aether SDK/cloud adapter and other OS backends were not built or run here.

## Final manual delivery

Both isolated Debug load-only products are available under `outputs/`.
The iOS product's install on the old device stalled after acceptance, so its
per-device runtime was stopped without erasing data. A new iPhone15Pro simulator
`6ADD5845-5369-40A6-86C9-35A78044619A` was created. CoreSimulatorService was
restarted; the fresh device's first migration was initially interrupted too
early, then resumed and finished in46sec on the final boot. No library/app code
or simulator data repair/reset was used for these host infrastructure issues.

The final iOS Latest app installed/launched as PID98069 under independent bundle
ID `org.apptraverse.WindowCatalog.ios.latest`. Matching supply hash was verified
before copying7103-byte USER from the original consumer; original USER hash
remained `33a99e87aa223686dd69d843458ba84e9bc145be51f2a55f8de19fb34dc77655`.
Native screenshot confirms the restored main catalog: Window1, Window2 and
selected Window3(Closed), with Add/Open/Delete/RU controls and three pages.
This is final-product launch/restore visual smoke, separate from full fixture
acceptance above. The old device/profile is preserved and shut down; latest
app is left running on the new simulator. macOS Latest remains running too.

Artifacts: `outputs/WindowCatalog macOS Latest.app`,
`outputs/WindowCatalog iOS Latest.app`, and
`outputs/WindowCatalog-Apple-Latest-report/` (logs, structured acceptance evidence,
final iOS screenshot; no USER binaries). Both deliverables use the built
load-only executables, with isolated bundle identifiers/ad-hoc signatures.
