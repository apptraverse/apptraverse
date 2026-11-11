# WindowCatalog Linux port — 2026-11-10

Base: `e48db8300b16b50d5ec375fa6a1b526b2f709853`, branch
`feature/domain-hierarchy-cloning`. Scope: Linux GTK WindowCatalog through
WSL/WSLg, its shared model/headless tests, and preservation of Windows behavior.
Other example applications and CI are excluded.

## Acceptance status

**Linux build and execution are BLOCKED / NOT RUN.** Even `wsl --list`
returns `Wsl/E_ACCESSDENIED` in the current Windows agent session. The user
authorized WSL access; granting workspace writes and network did not change
this result. No distribution, compiler, GTK development package or display
availability has been verified in this session. Existing historical Linux
PASS records do not verify this source revision.

The code is prepared for GTK3/X11 acceptance on WSLg. Native Wayland is outside
this geometry/stacking acceptance. No Linux release build or binary-size claim.

## Port changes

- The normal Debug application owns fresh graph creation and `--distill-only`,
  which runs before GTK initialization. It never restores previous USER.
- `linux_window_catalog_demo_load_only` embeds the Linux supply prepared by
  Debug and restores one `user.bin`. The common lifecycle still owns RAM USER
  and writes the file only after application shutdown. No separate exporter.
- One registration entry point runs before graph loading. Runtime presenter
  registration and graph construction are separate object files.
- The same `WindowCatalogLinuxApplication` host composition is used by the
  executable and the test runners: native Close flushes pending frame reports,
  queues z-order, stops/drains the model, waits for completion, and unloads UI.
- GTK list selection/activation captures a holder ID before enqueueing the
  command. Native geometry enters through Events/the existing main-frame queue;
  mirror publication no longer reposts viewport changes.
- GTK uses actual frame coordinates and client dimensions, leaves unspecified
  initial placement to the window manager, reports screen work area to the
  model, and distinguishes restore geometry from maximized/fullscreen live size.
- Mandatory distilled links and native lifetime order replace old locale,
  geometry and optional-parent fallbacks. Closed/lazy windows remain valid
  lifecycle alternatives and are not loaded by z-order inspection.
- Session automation moved from the application into `tests`. Production has
  no automation argument or shutdown markers. Existing legacy demos are gated
  by the existing legacy-examples option.

## Prepared Linux tests (not executed)

The existing shared headless, embedded-overlay, persistence-boundary,
close-stage and frame-coalescing sources are linked to the Linux graph.
The native runner uses the production host composition and real GTK
button/list/window signals. It is a test executable, not automation embedded
in the ordinary application entry point.

The X11 smoke covers Add/Close/Reopen/Delete, stable row identity and activation,
localization, native geometry, maximization/restore, several cold restarts,
two fresh Debug launches into the same parent, and load-only restoration of
the first Debug USER. It verifies that USER is unchanged at startup and during
publications/child Close, and contains exactly one file after shutdown.

Relevant targets:

```text
linux_window_catalog_demo
linux_window_catalog_demo_load_only
apptraverse_window_catalog_linux_acceptance_runner
apptraverse_window_catalog_linux_fresh_runner   (Debug only)
apptraverse_window_catalog_linux_l3_smoke       (build + native test)
```

Use the existing Linux build/cache when accessible. The declared default is
`linux-x64-ninja-clang-debug`; do not clean Windows/Linux build trees or fetch
fresh dependencies merely to resume this work. Before native acceptance,
inspect the installed WSL distribution, toolchain and WSLg display. The smoke
sets `GDK_BACKEND=x11` and creates separate temporary profiles.

## Executed checks so far

The port exposed a shared publication defect: a programmatic frame request on
a nested desktop window was not delivered when the runtime published the
application root. Structural publications now include the requested window IDs
collected from the existing serialization scratch graph, without another Save
pass. Ordinary snapshots do not consume requests. Existing presenters apply
their own requests; newly loaded presenters use their normal OnLoad frame.
The persistent S0/USER and initial-publication formats are unchanged.

`apptraverse_nested_window_frame_publication_test` was built and run before
the serializer fix: **FAIL**, exit 1 at the first missing nested request.
With the fix: **PASS**. It exercises independent windows, alias references,
latest request coalescing, root-window publication, repeated publication,
ordinary snapshots, native/maximized reports without feedback, centered
placement through screen Events, and a newly materialized presenter.
Logs: `.artifacts/linux-port-nested-frame-before.log` and
`.artifacts/linux-port-nested-frame-after.log`.

- Windows incremental Debug build of the five shared Catalog targets: PASS.
- Shared Catalog headless, close-stage, USER boundary, embedded overlay and
  main-frame coalescing: **5/5 PASS, 18.20 seconds**, with an isolated temporary
  directory under `.artifacts/linux-port-windows-temp`.
- The first run using the default temporary directory failed three file-based
  tests. The isolated repeat passed without a source change; this is recorded
  separately from Linux acceptance.
- Python smoke syntax compilation: PASS. This is not a native smoke result.

After the shared fix, the existing Windows Debug tree was incrementally rebuilt
for both application executables and 23 test executables. The final CTest run
was **24/24 PASS, 101.94 seconds**: the preceding 22-test Windows/headless scope
plus the nested-frame regression and desktop geometry test. It includes native
acceptance, frame sync/drag/publication, command routing, model/headless,
clone/domain/history, USER boundary/single-file storage, model/UI sessions,
chrome adaptation, configuration selection, and the **5x4 Close diagnostic**.
No MinSizeRel build or run was performed in this iteration.

Build/log evidence: `.artifacts/linux-port-windows-build.log`,
`.artifacts/linux-port-windows-tests.log`,
`.artifacts/linux-port-windows-tests.xml`. Configuration remained Debug,
`AE_NO_DEBUG_LOG=ON`, with the existing verified dependency source and caches.
No clean or dependency re-download. Independent source review found no further
blocker in the edited Linux composition or shared request delivery; it does
not replace Linux compilation/execution. `git diff --check`: PASS.

## Remaining acceptance

1. Enable a functioning WSL execution environment and inspect its existing
   Linux build/cache and GTK3/X11 tools.
2. Build the normal Debug application, embedded consumer, headless tests and
   both native test runners incrementally; fix actual compile/runtime failures.
3. Run the Linux headless and native suites, then launch the ordinary Linux
   application once for manual inspection.
4. Record actual Linux results and any remaining platform limitations. A
   source review or a Windows PASS must not be presented as Linux acceptance.
