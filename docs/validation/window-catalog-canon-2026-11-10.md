# WindowCatalog canon completion — 2026-11-10

Starting revision: `3fb125c9653a58620bfdce9a172ab2c4582e63ee`.
Scope: Windows WindowCatalog, its headless coverage and shared dependencies.
The user explicitly excluded CI and other example applications. Their existing
issues are deferred, not fixed or accepted as conforming.

## Changes

- The optional in-content Close prefab now contains its required catalog link
  at construction. Removed `WireWindowCloseButton`: Clone must preserve the
  external catalog and remap internal window/button/presenter links itself.
  Ordinary loading and lazy flags remain lifecycle operations.
- Added a direct prefab/Clone regression before `AddWindow`: required source
  identity, new internal IDs, exact target catalog instance, internal cycles,
  source-domain destruction and cold round-trip. Source loading can materialize
  stored ancestor layers in the temporary domain; exact target-instance identity
  is required after Clone, not for that intermediate stored representation.
- The Æther factory lookup now locates the raw stored definition before
  filtering known classes. Parent fallback occurs only when a local definition
  is absent. Unknown derived classes at the definition owner reject the load;
  a valid local definition still shadows an incompatible ancestor.
- Added parent/grandparent unknown-class rejection and local-shadow tests using
  public loading APIs. These preserve the existing class-loading contract; no
  application migration, rollback or recovery mechanism was added.
- Moved three queue-binding inspection/teardown helpers from production into
  the chrome-adaptive test. Actual runtime queue attachment remains in the app.
- Narrowed agent guidance and the active plan to the user's WindowCatalog-only
  scope. No CI, legacy target or other application repair was made.

## Evidence

Before production fixes, both new regressions failed on the existing code:

- hierarchy: `reject unknown parent class before activation`;
- prefab: `prefab.close_button->catalog.is_valid()`.

Log: `.artifacts/catalog-canon-before-fix.log` (0/2 PASS).
The initial test draft used private Domain methods and was corrected to public
Registry/loading APIs before this runtime reproduction. The initial prefab
pointer-identity assertion was also corrected for the documented temporary
source-domain materialization contract; final clone identity checks remain.

Offline verification applied all **27 patches** to an isolated archive of the
pinned Æther commit `79283b667cabf689da1059d618fe2a22abf51db4`: PASS.
Evidence: `.artifacts/patch-chain-verification/20261111T031551101092Z/result.json`.
The effective dependency used by the incremental Debug build has the same
normalized `domain.cpp` content. Dependency caches were not cleared or fetched.

Focused fixed checks passed: hierarchy, direct-clone/headless and native chrome
adaptation. Final incremental Windows Debug (`AE_NO_DEBUG_LOG=ON`) build PASS;
final regression **18/18 PASS**, **101.63 s**, including all Windows Catalog
tests, the 5×4 Close diagnostic, relevant Clone/history/scope/publication/USER
tests and native command routing. Logs: `.artifacts/catalog-canon-final-build.log`,
`.artifacts/catalog-canon-final-tests.log` and
`.artifacts/catalog-canon-final-detail.log`. No production changes followed this
final run.

## Remaining scope boundaries

The prior tracing finding C1 was resolved in `3fb125c`; this iteration closes C2
and C3. Stale CI (C4) and the default-build inclusion/linking of old Surface
examples are excluded by the user. No further concrete violations were found
in the reviewed active Windows Catalog paths. This is bounded source review
and test evidence, not proof for all theoretical graphs or platforms.

No MinSizeRel, other-platform/device or standalone upstream Æther suite is
claimed by this iteration. Only the existing Debug build is used.
