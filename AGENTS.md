# Agent rules (apptraverse — unified worktree)

- **Never** add an AeroAdmin dependency or copy AeroAdmin sources.
- Windows is the first platform; the common model must stay cross-platform (no HWND, Win32 paths, or Win32 keycodes in `src/common/`).
- Model first: `surfaces_demo` / WindowCatalog semantics — model thread → publication → UI mirror → presenters; GUI → `ModelObjectProxy` → model Events.
- UI mirror and presenters must not mutate persistent model directly.
- Keep presenters thin; one logical control → one presenter responsibility where practical.
- Persistent vs runtime separation is mandatory (HWND, transport handles are runtime-only).
- Demo catalog uses checkpoint + `DirectoryDomainStorage`; see [window_catalog_demo.md](docs/window_catalog_demo.md).
- Prefer incremental **Debug** builds; do not clean/rebuild without cause. **No** Release by default, no fresh populate without user request.
- Update [docs/versioning/PROGRESS.md](docs/versioning/PROGRESS.md) only after verified steps. Do not mark PASS without execution.
- Do not merge to `main` or force-push without explicit user instruction (task may specify `--force-with-lease` on `integration/unified-application`).
- Рабочие изменения: commit + push в текущую ветку. PR — только по прямой команде.
- Distilled UI graph contract is mandatory (button, label, locale, window_presenter, links): do not re-validate with optional runtime checks, fallbacks, or silent skips in presenters.
- Runtime lifecycle states (`ReadyForPresentation`, activated parent, disabled control, closed/lazy window) are real; do not conflate them with missing graph links.
- Side effects (Save/Load/Inject, I/O, mutation) must not run only inside `assert`; execute once, store the result, assert on that result (must still run under `NDEBUG`).

## Active plan (P0–P6)

- **Order:** [docs/versioning/PLAN.md §P0–P6](docs/versioning/PLAN.md#активный-план-мультиплатформенный-windowcatalog-p0p6) — **only** active queue. **U5.2–U6**, **9D–9H** are **not** next steps.
- **Priority:** кроссплатформенный WindowCatalog E2E (Windows → Web → далее P4) и приложение администрирования на том же template после первого non-Windows UI.
- **Deferred (не возвращать в активную очередь без отдельного задания):** Object Core T16/T17, полная матрица Clone/upgrade, `kSupplyConstantIds` replacement, production hardening, resource download matrices — см. PLAN §P3 DEFERRED.
- **Canon:** [docs/architecture/AppTraverse_Architecture_RU.md](docs/architecture/AppTraverse_Architecture_RU.md) (`8c863bc` observations).
- **SharedNode / replication / Æther network client:** **full removal** (**P1.1**), not OFF or stubs.
- **U5.1** (`8c863bc`) shutdown-save exit code — keep; do not revert.
- No production-hardening (corrupt checkpoint, recovery, disk-full) unless explicitly assigned.
