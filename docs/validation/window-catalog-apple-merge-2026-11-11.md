# Apple WindowCatalog branch integration — 2026-11-11

The user requested merging the completed MacBook work into the current branch.
Target: `feature/domain-hierarchy-cloning` at
`1aff49e03ff24777bd7363fefd7d19fb37124e49`.
Source: `origin/feature/apple-window-catalog` at `d55f4bf`.
Merge base: `b0c808d81b187b3fa890b0a59d25e5a7bf23c66a`.
The merge preserves both parent histories and all five Apple commits.

## Integration decisions

Both branches independently implemented responsive mobile child layout. Keeping
both implementations would duplicate the submission method and introduce two
different event/history owners for the same operation. The merged source keeps
the current `CatalogWindow : NodeFor` and child-owned
`SetCatalogChildViewportEvent`, including explicit width, height and density,
replay, native-geometry separation and closed/lazy-window handling.

Apple's duplicate catalog-owned `SetWindowCatalogChildClientViewportEvent` and
main-DPI propagation were not retained. The existing Android/Web per-child DPI
route remains unchanged; iOS reports points with density160. The iOS composition
now passes explicit `std::int32_t{160}` through `ModelObjectProxy::Invoke`.
The old three-argument call cannot invoke a four-argument member-function pointer
using C++ default arguments, so simply keeping the current shared declaration
would have broken the Apple build.

The headless test now compiles/executes that exact proxy route for portrait
393x759 and landscape759x393, and checks the Apple tiny40x60 viewport case.
Existing density-change, replay, restore-frame, late-close and cold USER tests
remain. Shared production model/event/layout code is byte-identical to the
target parent. No USER migration or additional event identity was introduced.

All platform implementation files, native lifecycle changes, Apple build scripts,
acceptance targets and evidence documents from the source branch are retained.
The two progress histories are concatenated, not replaced. Apple CMake additions
are guarded by the platform/target conditions; Windows/Linux keep their existing
toolchains and caches. The current USER byte codec and browser lifecycle fix
remain intact. Main, other examples and maximization are outside this merge.

## Apple verification boundary

Upstream MacBook reports remain attached to their original scope and commits:
macOS native/lifecycle/headless3/3 plus common6/6; iOS three original acceptance
processes and two actual OS-deactivation/cold processes. See the imported
macOS native-click and iOS lifecycle validation documents.

This integration is performed on Windows with WSL. macOS/iOS native compilation
and execution of the merged revision are **NOT RUN**: no Apple toolchain/host is
attached to this session. The portable headless proxy test checks the shared
signature and model behavior, not Swift/UIKit/AppKit compilation. A MacBook
incremental build and its native acceptance should verify the merged revision
before claiming new Apple runtime acceptance.

Normal Debug's fresh-profile policy is unchanged. Persistence profiles must use
their matching embedded BACKING; no rebuilt supply is installed over the manual
Android/Web profiles as part of this integration.

## Executed integration checks

- Windows incremental Debug build: PASS, 73 Ninja steps. Selected regression
  **24/24 PASS, 91.89 s**, including the expanded headless test (7.23 s).
  Native acceptance separately **1/1 PASS, 16.81 s** after a single initial
  main-window foreground activation. No child-focus assertion was assisted.
  Combined scope: **25/25**; existing dependency cache and isolated TEMP retained.
- Windows Close five-by-four diagnostic PASS; HWND medians
  **209/219/188/136 ms**. Parallel compilation makes this a functional observation,
  not a comparative performance measurement.
- Linux incremental Debug build **62/62 steps PASS**; historical selected scope
  **15/15 PASS, 46.72 s**, private Xvfb/Openbox wrapper48.83 s. Expanded headless
  PASS1.34 s, native acceptance PASS43.62 s. WSLg's previously documented exact
  positioning limitation remains outside this private-X11 acceptance.
- Android persistent installed APK: **6/7 in 113.107 s**, then unchanged focused
  rotation/Activity-remount case **PASS, 13.261 s**. The batch missed its final
  horizontal navigation; remount and same-process checks had already passed.
  This is seven-case coverage across two runs, not a clean batch claim. No APK
  rebuild/install or test change was made. The default USER remained byte-identical
  before/after tests and ordinary launcher; current RU Window2 restored and was
  left running. Existing Android runtime/codec source is unchanged by the merge.
- Web unchanged artifacts: functional browser test PASS, full headed lifecycle
  **PASS, 16.391 s**, Node embedded-session test PASS. Native browser-window close,
  tab close/reload, hide/return and cold restoration retain the previous scope.
  Existing Web runtime/codec source is unchanged by the merge.
- No unresolved merge markers; staged diff whitespace checks PASS.

Evidence under `.artifacts/`: `apple-merge-windows-{build,tests,detail}.log`,
`apple-merge-windows-native-tests.log`, `apple-merge-android-tests.txt`,
`apple-merge-android-rotation-focused.txt`, `apple-merge-android-web-retest.md`,
`apple-merge-web-{functional,lifecycle,node}.log`. Test-only caches and artifacts
are not committed.
Linux evidence: `apple-merge-linux-{build,tests,x11}.log` and
`apple-merge-linux-tests.xml`.
