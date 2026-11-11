# WindowCatalog Linux port — 2026-11-10

Base implementation: `b0c808d81b187b3fa890b0a59d25e5a7bf23c66a`, branch
`feature/domain-hierarchy-cloning`. Scope: GTK3/X11 WindowCatalog, shared
headless tests, and preservation of Windows behavior.

## Current acceptance

- Ubuntu 24.04.2 through WSL, Clang 18.1.3, GTK 3.24.41, X11 1.8.7.
- Debug build and Debug-owned distillation: PASS. `AE_NO_DEBUG_LOG=ON`.
- Linux selected CTest scope: **15/15 PASS, 46.16 s**. Native acceptance:
  **43.53 s**, twelve subprocess phases. The native tests used a private
  Xvfb/Openbox X11 display; WSLg's desktop was not modified.
- Existing Windows incremental Debug scope: **24/24 PASS, 84.26 s**, including
  native acceptance and the 5x4 Close diagnostic. No Windows source changes.
- Ordinary Linux Debug application launched once on WSLg with a separate fresh
  profile. Process survived startup and X11 exposed its `Window Catalog` window.
  This launch is not evidence of WSLg geometry acceptance.

**Known platform limitation:** WSLg 1.0.79's own X11 window manager reports
`(58,38)` after `gtk_window_move(90,70)` with the default gravity. The ordinary
geometry test therefore stalls at the requested coordinates on that display.
The same test reports `(90,70)` under Openbox. Static gravity and GTK_CSD probes
were not adopted as fixes; no hardcoded shadow offsets were added. Exact
WSLg desktop position round-trips remain unverified/failing, independently of
standard Linux X11 acceptance. Native Wayland and Linux release are NOT RUN.

**Maximization is explicitly DEFERRED at the user's request.** The provisional
maximize/restore fixes were reverted and the acceptance phase now checks only
initial OS placement. No new maximization model, persistence or transition
mechanism was added. Its observed restore-frame problem remains unfixed.

## Corrections from actual Linux execution

1. The test incorrectly tried loading a fresh Debug USER with an unrelated
   build-time embedded BACKING. Each fresh distillation creates new object IDs;
   USER deliberately omits supply constants and unchanged BACKING layers.
   Those two supplies cannot be interchanged. Fresh testing now verifies two
   independent profiles, exactly one nonempty user.bin per profile, and that
   the second launch leaves the first USER's bytes/timestamp untouched. Cold
   restart acceptance uses one matching embedded supply throughout. No runtime
   repair, copying constants into USER, migration or ID redesign was introduced.
2. Ordinary native shrinking was blocked by GtkFixed propagating the previous
   child layout as the top-level minimum size: requested **600x450**, actual
   **648x488**, reproduced under Openbox without maximizing. The backend now
   uses a GtkFixed-compatible client mount whose children do not impose the
   window's minimum size. Model child positions and normal GTK allocation remain
   unchanged. The focused test FAILed with stock GtkFixed and PASSed with the
   new mount. This fix is for ordinary resizing, not maximization support.

## Implemented port contract

The application uses the shared model runtime, publication/UI mirror and
application session template. Native GTK signals route commands through model
proxies, with stable holder IDs for list selection/activation. One application
registration entry point anchors the graph's schemas. Graph construction and
runtime registration occupy separate object files.

Normal Debug creates a fresh graph and owns `--distill-only`; no standalone
Linux exporter. The explicit load-only consumer embeds the Debug-produced
supply. USER lives in RamDomainStorage: child Close saves into RAM, application
shutdown writes exactly one user.bin. Model/Event logic remains cross-platform.

The native test runner shares production `WindowCatalogLinuxApplication` host
composition. Automation is test-only. Acceptance covers fresh profiles, initial
OS placement, Add, row activation/identity, Close/Reopen/Delete, localization,
ordinary move/resize, native stacking, several cold restarts, unchanged USER
during startup/publications/child Close, and single-file shutdown persistence.
The earlier shared nested-frame publication fix remains covered by its existing
regression and by Windows/headless tests.

## Reproduction and evidence

Build directory: `build/linux-x64-ninja-clang-debug`. The first Linux configure
reused existing dependencies from `.cpm_cache_isolated_v3` and the same verified
patched Æther source used by Windows:
`.artifacts/patch-chain-verification/20261110T180342637249Z/tree`.
No clean rebuild of Windows and no fresh dependency population.

Relevant targets:

```text
linux_window_catalog_demo
linux_window_catalog_demo_load_only
apptraverse_window_catalog_linux_acceptance_runner
apptraverse_window_catalog_linux_fresh_runner
apptraverse_linux_ui_client_mount_test
apptraverse_window_catalog_linux_l3_smoke
```

Run native acceptance on a normal X11 desktop with GTK3. For a headless Linux
host, start Xvfb plus Openbox on a private DISPLAY before CTest; Xvfb alone does
not provide window placement/stacking. The smoke selects `GDK_BACKEND=x11`.

```sh
cmake --build --preset linux-x64-ninja-clang-debug --target apptraverse_window_catalog_linux_l3_smoke
```

Logs under `.artifacts/`:

- `linux-port-build.log`, `linux-port-tests.log`, `linux-port-tests.xml`
- `linux-x11-acceptance.log`, `linux-x11-acceptance-before.log`, `linux-x11-frames.log`
- `linux-client-mount-before.log`, `linux-client-mount-after.log`
- `linux-port-windows-recheck-build.log`, `linux-port-windows-recheck-tests.log`,
  `linux-port-windows-recheck-tests.xml`, `linux-port-windows-recheck-detail.log`
- `linux-manual-launch.log`, `linux-manual-launch.json`

Windows child Close medians (cold/burst/settled/saved): **189/208/181/131 ms**;
Main exit medians: **48/38/48/65 ms**. These are Windows Debug measurements,
not Linux timings.

Next authorized work: Android, then WebAssembly. macOS/iOS are being developed
independently in another branch and are not modified or merged by this task.