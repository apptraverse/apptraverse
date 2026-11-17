# Non-Apple layout and LOCAL Canon corrections — 2026-11-16

## Authority and ownership

The user authorized Windows/common corrections and Linux, Web and Android rollout.
Apple-specific implementation and runtime acceptance remain excluded. The completed
[GPT-6 Astra ultra audit](windows-canon-ultra-audit-2026-11-16.md) considered all 30
mandatory Canon rules. Implementation is owned exclusively by GPT-6.1 Sol high:
one worker owns common/Windows, one Linux/Web, and one Android. Astra performs
read-only auditing. No automatic migration of experimental profiles is introduced.
No commit, push or merge is part of a worker's completion boundary; the coordinating
agent aggregates the verified work.

## Implementation

- A1: Windows native frame, viewport, screen, DPI and computed-layout observations
  no longer call the portable frame/chrome pipeline. Native measurements remain in
  presenters and the explicitly persisted window facts/placement travel through
  UiDeviceState LOCAL Events. Portable explicit semantic RequestFrame remains.
  The analogous migrated adapters are owned and verified by the port workers.
- A2: standard UiPresentedWindow now composes required typed layout_state and
  device_state links. UiWindowPresenter resolves LayoutRoot and LocalDeviceState
  through those links; WindowCatalog-specific discovery overrides are removed.
  NativeCaptionProvidesClose is explicit backend policy, combined with composed
  caption_close_control identity/control slot. Both presentation and the effective
  local constraint tree omit that affordance when the caption supplies it.
- Generic RunClientLocalApplicationModelSession and the dual-RAM embedded-session
  overload discover supply-marked roots, route USER/LOCAL before Load and execute
  the model prepare hook before initial publication. Windows Catalog and MinimalApp
  use this composition; platform lifecycle owners persist both ordinary stores.
- Generic historical capture collects shared-reference policies from all captured
  live Nodes. The new unhydrated destination base is excluded. Standard windows
  retain their device identity and nested UiLayoutState retains its supply
  definition; child-local override state still belongs to the clone.
- A3: ObserveWindow commits its observation/preference then resolves against the
  accepted environment through a separate monitor-resolution Event. Queued reports
  for disappeared monitors cannot leave a fabricated kFound. Replay Apply neither
  consults the OS nor commits nested Events.
- A4: Catalog and independent MinimalApp builders explicitly Bootstrap device and
  layout initial values before runtime history initialization/distillation.
- Windows only suppresses a first placement publication when it is revision 1 and
  matches the already observed native placement. A coalesced later resolution is
  applied. Moving to a different monitor stages a small frame entirely inside its
  work area before reading the destination HWND DPI, preventing a large source
  frame from selecting the wrong monitor by overlap.

## Executed evidence so far

All Windows builds use the existing incremental Debug cache
C:/Users/nickc/Documents/Codex/build-window-layout-win7, VS2022 desktop v143
MSVC14.44 and the existing patched dependency caches. No dependency fetch or clean
build was performed.

| Check | Result |
| --- | --- |
| Current Windows Catalog and MinimalApp Debug/load-only targets, matching supply generation | PASS |
| UiDeviceState ordering, empty-monitor/queued first+user observations, replay and immediate compaction | PASS, 0.19s |
| UiLayoutState supply/override/publication/cold-load suite | PASS, 0.17s |
| WindowCatalog headless suite | PASS, 36.76s |
| MinimalApp headless suite including independent generic dual-RAM composition | PASS, 25.46s |
| Windows adaptive/chrome suite including coalesced first placement resolution | PASS, 14.47s |
| Full Windows native catalog lifecycle acceptance, first attempt | Foreground precondition failed after 22.10s; no PASS claimed |

The new MinimalApp test contains no WindowCatalog dependency. It verifies required
standard links, startup preparation before first publication, native-format LOCAL
persistence across three fresh Domains, unchanged-startup no-op, immediate window
history compaction/replay, shared definition/device identity, USER exclusion and
absent/existing LOCAL seed precedence. Existing application lifecycle suites remain
part of regression acceptance.

Native resize checks assert unchanged portable viewport, window generation,
semantic restore dimensions and layout inputs while actual LOCAL placement updates.
The coalesced publication test queues native startup observation followed by a
work-area resolution before publishing and checks the actual HWND receives it.

The first full native acceptance attempt failed at the existing assertion that
GetForegroundWindow equals the requested test parent. A newly created child could
cover the requested title bar when programmatic foreground activation was denied.
The harness exposes its own requested window before the existing user-equivalent
click; the foreground assertion remains unchanged. This is a harness precondition,
not a relaxation of application focus correctness.

A matching actual native-saved Windows USER+LOCAL source was preserved at
outputs/canon-fixes/windows-source before rerunning acceptance. Port workers copy
it before consuming it; their transfer results are recorded in their reports.

## Remaining acceptance boundaries

Full incremental build/regression and final native lifecycle/PE import results are
recorded below when complete. Actual Win7, physical mixed-DPI monitors, real
screen-reader operation, system High Contrast and the complete accessibility
matrix remain NOT RUN unless separately executed. A single-monitor native run and
headless ordering tests do not prove physical mixed-monitor behavior.

Linux/Web and Android evidence belongs to their matching current validation reports;
historical PASS records are preserved at their original scope. The final read-only
Astra ultra follow-up must revisit all 30 rules, distinguish fixed findings from
unexecuted hardware boundaries, and must not claim build success as runtime proof.


## Final Windows/common verification — completed 2026-11-17

The final per-test outcomes are **28 distinct tests PASS**, collected across the
full affected model regression and narrow corrected-fixture reruns. This is not a
claim that the initial combined invocation passed: its four bare-window fixtures
were updated to compose the new required services explicitly, and each was rerun.
The expensive WindowCatalog model suite passed in 294.80s and was not repeated
for subsequent fixture-only changes. The exact per-test result/log mapping is
outputs/canon-fixes/final-windows-test-results.json.

| Final boundary | Result |
| --- | --- |
| Incremental full Debug build, existing v143/cache | PASS, 379-object schema rebuild and subsequent incremental links |
| Affected model/history/clone/layout/application/lifecycle tests | 18 distinct tests PASS across final per-test results |
| Windows native button/fatal/HWND/title/frame/drag/publication/chrome tests plus MinimalApp native acceptance | 9 distinct tests PASS |
| Full Catalog native lifecycle acceptance, dedicated rerun | PASS, 32.32s |
| MinimalApp independent native USER+LOCAL cold persistence | PASS, 3.16s |
| PE imports and subsystem headers for all four Debug executables | PASS within static inspection scope |
| git diff --check | PASS; existing LF/CRLF conversion warning is not a whitespace error |

The two rewritten obsolete portable-native-frame tests retain concrete behavior:

- presenter_win_desktop_interactive_drag_e2e_test performs fifty real HWND drags,
  LOCAL observation through the proxy/model queue, publication, stale publication,
  real 700x520 resize, ordinary object-storage save, a fresh Domain load and a
  newly created HWND with the persisted LOCAL position/size. The portable semantic
  frame/generation remains unchanged. DIP-to-pixel integer rounding is tolerated
  by two pixels at the observed 192-DPI host setting.
- presenter_win_desktop_frame_publication_e2e_test performs real native ABA moves
  with delayed publications, preserves the portable generation, applies an explicit
  semantic RequestFrame, and confirms a materialized language publication does not
  move the native window. The previous assertions that native drags overwrite
  portable x/y are intentionally retired.
- presenter_win_desktop_frame_sync_test retains stale portable publication and
  explicit semantic request behavior; native observation assertions now inspect
  LOCAL facts/preference rather than portable native-pixel Events.

The MinimalApp harness now declares DPI awareness before launching the aware app.
Its former unaware coordinates were virtualized: at 192 DPI the harness expected
220/140 while LOCAL correctly stored native 440/280. The corrected harness still
asserts actual LOCAL DIP-to-desktop-pixel projection and native cold restore. Its
profile reader routes both ordinary stores before Load and asserts that the
portable semantic frame remains the supplied 120/80/640/480.

A temporary link failure occurred when the running long model test held its exe;
all Windows build/test operations were then serialized and the incremental relink
completed. No unresolved failure from those attempts is recorded as PASS.

The four final executable copies are in outputs/canon-fixes/binaries, with matching
*.imports.txt and *.headers.txt beside that directory. Direct imports are only
USER32, GDI32, KERNEL32 and the v143 Debug CRT DLLs. No mandatory CreateFile2,
CopyFile2, GetDpiForWindow, GetDpiForMonitor, RoGetActivationFactory or
SetProcessDpiAwareness import is present. Catalog subsystem version is 6.01;
MinimalApp is 6.00. This does not substitute for an actual Win7 run or provision
its compatible Debug CRT.

| Executable | SHA-256 |
| --- | --- |
| win32_window_catalog_demo.exe | 359869BB422D78C4F23FFD499C099DAC9174E2DAB095FC683FABC022DD8F9B86 |
| win32_window_catalog_demo_load_only.exe | D4029AA26BD698FD6F0F8ABB1A2594A6822E236116FA74B4C47C38D1BEC19C54 |
| win32_minimal_app.exe | 78C495D1DE2971262E7BF7F74E4CDACF99F04B80D9CE49E17AF602EFE90291D1 |
| win32_minimal_app_load_only.exe | 16DAEAF2193DE414E374CD638B75ADAD8D1E79A0FEEF129EF0143968BDC7ABBA |

Actual Win7, physical mixed-DPI monitor transfer, real screen-reader operation,
High Contrast toggling and full system text-scale accessibility acceptance remain
NOT RUN. Destination monitor staging and coalesced placement fixes are implemented;
publication ordering has native coverage, while physical mixed-monitor behavior
retains its explicit unavailable-hardware boundary. Apple files were not edited.


## Post-fix audit B3 — explicit same-value frame intent

Astra's follow-up found that after a LOCAL-only native drag, an explicit
RequestFrame equal to the portable semantic values returned before presentation.
The command now marks runtime pending publication and materialized change even
when those data values are unchanged. Duplicate pending requests coalesce; the
journal receives no redundant Event. Replay Apply never manufactures this marker.

The real HWND publication test now drags away, requests the original exact
100/100/640/480 semantic frame, verifies unchanged journal size, publishes and
checks actual native position/size restored. Headless geometry tests verify
notification/generation, pending coalescing, one-shot consumption and pure replay.
The headless application contract now expects delivery of repeated explicit
intent while retaining one persistent frame Event.

Six affected tests PASS (canon-b3-tests.log, 3.15s): desktop geometry, headless
application contract, nested-window frame publication, native frame sync, native
drag/resize/cold restore, and native delayed/ABA publication including the new
same-target regression. This adds nested-window publication to the earlier set:
29 distinct final per-test outcomes are now recorded. Previously passing expensive
model/clone suites were not repeated for this narrow command-intent change.
The four shipped executables were relinked, copied and their PE imports/headers
rechecked; the SHA-256 table above contains the refreshed B3 artifact values.

The LOCAL contract now explicitly distinguishes Windows/GTK outer desktop frames
in 96-DIP coordinates from Android client surfaces in 160-density logical dp and
Web client surfaces in CSS pixels. It does not promise physical desktop window
placement for Android/Web. Port workers confirmed Web projection and aligned the
Android observation formula with its logical-surface solver convention.


## Coordinated final scope — 2026-11-17

All 30 mandatory Canon rules have an explicit final disposition in the
[read-only Astra audit](windows-canon-ultra-audit-2026-11-16.md). This closes the
reviewed source-correction scope; it is not certification of every possible
runtime, OS image or hardware configuration. All coding remained GPT-6.1 Sol high.

Windows/common final evidence is 29 distinct PASS outcomes, with the B3 narrow
rerun and refreshed executable/static-import evidence above. The
[Linux/Web report](window-catalog-linux-web-local-layout-2026-11-17.md) records
actual GUI/browser and lifecycle PASS, actual native Windows USER transfers with
absent/existing LOCAL, and real compatible supply/default/override/Clear updates.
The [Android report](window-catalog-android-settings-2026-11-17.md) records PASS
for explicit Windows transfers, actual compatible APK/supply update, live/stopped
OS settings and unchanged-startup comparison, the completed B1 missing-binding
negative, independent MinimalApp and ordinary Debug fresh-policy checks.

Android has seven distinct baseline functional UI cases plus the new native
disabled-state case PASS, and two native Recents Home/Back dismissal cases NOT
PASS. The independent bounded diagnosis reproduced
the same failure on stock Settings task 911 using native thumbnail-center gestures,
with tap delivery working, task-pinning disabled and unchanged target task IDs.
Temporarily normal animation scales and an isolated launcher restart did not
restore dismissal. This is evidence for an emulator launcher/input boundary;
it does not prove successful dismissal or justify weakening the task-removal
assertion. onStop persistence/cold-load evidence remains separately scoped.
Raw evidence: ../outputs/android-acceptance/sol-independent-recents-diagnosis.md
and its stock comparison logs, hierarchies and screenshot. No blanket Android
9/9 PASS or new 52-process portability-matrix claim is made.

The additional final palette-only native Android appearance/disabled-state
regression passed 2/2 narrow cases in 52.929s, recorded in
../outputs/android-acceptance/instrument-appearance-final.log. It retests
Auto/Light/Dark/soft commands and LOCAL cold restore and adds the native-widget
disabled text-state comparison against Theme.Material. This narrow PASS does not
replace or promote the original full functional-suite outcome. The default12 APK
build passed in 16s and the isolated compatible variant24 build passed in 12s; exact
final artifact hashes and packaging evidence belong to the Android report.
Actual system-image replacement/OTA, actual Win7, physical mixed-DPI monitor
acceptance, real screen-reader/full High Contrast/accessibility matrices, Apple
implementation/runtime and Release packaging remain NOT RUN. The platform reports
preserve their further specific unexecuted boundaries.
