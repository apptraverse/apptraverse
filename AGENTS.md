# Agent rules (apptraverse — unified worktree)

- **Never** add an AeroAdmin dependency or copy AeroAdmin sources.
- Windows is the first platform; the common model must stay cross-platform (no HWND, Win32 paths, or Win32 keycodes in `src/common/`).
- Model first: `surfaces_demo` / WindowCatalog semantics — model thread → publication → UI mirror → presenters; GUI → `ModelObjectProxy` → model Events.
- UI mirror and presenters must not mutate persistent model directly.
- Keep presenters thin; one logical control → one presenter responsibility where practical.
- Persistent vs runtime separation is mandatory (HWND, transport handles are runtime-only).
- Catalog persistence uses standard object storage (P2.d.1R), with directory/embedded BACKING+USER composition as selected by the application; no separate checkpoint sidecar. See PLAN/PROGRESS for current evidence.
- Prefer incremental **Debug** builds; do not clean/rebuild without cause. **No** Release by default, no fresh populate without user request.
- Update [docs/versioning/PROGRESS.md](docs/versioning/PROGRESS.md) only after verified steps. Do not mark PASS without execution.
- Do not merge to `main` or force-push without explicit user instruction (task may specify `--force-with-lease` on `integration/unified-application`).
- Рабочие изменения: commit + push в текущую ветку. PR — только по прямой команде.
- Distilled UI graph contract is mandatory (button, label, locale, window_presenter, links): do not re-validate with optional runtime checks, fallbacks, or silent skips in presenters.
- Runtime lifecycle states (`ReadyForPresentation`, activated parent, disabled control, closed/lazy window) are real; do not conflate them with missing graph links.
- Side effects (Save/Load/Inject, I/O, mutation) must not run only inside `assert`; execute once, store the result, assert on that result (must still run under `NDEBUG`).

## Porting and regression (backend / template work)

When a defect appears during porting, fix the **architectural cause** in the right layer first. Then re-run existing E/E2E/smoke for **every backend already verified**, in the **same scope** as their historical PASS. Check the **same boundary** and **analogous call sites** for the same defect and broken shared contracts — not a repo-wide audit.

- Fix new blockers for the current scenario and re-verify before claiming PASS.
- Record separate non-blocking findings explicitly; do not mark them fixed.
- Missing toolchain/device → **BLOCKED / NOT RUN**; build alone is not runtime proof.
- Keep historical PASS records with their scope; record current regressions separately.
- Non-blocking Object Core matrices and production hardening stay **DEFERRED**.

Order unchanged: template/components on seven backends → WindowCatalog on them → administration.

## Active plan (P0–P6)

- **Order:** [docs/versioning/PLAN.md §P0–P6](docs/versioning/PLAN.md#активный-план-мультиплатформенный-windowcatalog-p0p6) — **only** active queue. **U5.2–U6**, **9D–9H** are **not** next steps.
- **Priority:** сначала библиотечный application template и controls на семи backend (Windows, Linux, macOS, Android, iOS/iPadOS, Web, Headless), затем WindowCatalog как тонкий потребитель на всех семи; администрирование только после P3/P4. Активные P3.T/P4.C — в PLAN.
- **Boundary:** common/backend libraries не зависят от examples/WindowCatalog; native host/controls принадлежат backend, модель/Events/prefab/composition — приложению. Независимый MinimalApp проверяет переиспользование. Не копировать SurfacesModelSession и mirror-код в новые порты.
- **Deferred (не возвращать в активную очередь без отдельного задания):** Object Core T16/T17, полная матрица Clone/upgrade, `kSupplyConstantIds` replacement, production hardening, resource download matrices — см. PLAN §P3 DEFERRED.
- **Canon:** [docs/architecture/AppTraverse_Architecture_RU.md](docs/architecture/AppTraverse_Architecture_RU.md) (`8c863bc` observations).
- **SharedNode / replication / Æther network client:** **full removal** (**P1.1**), not OFF or stubs.
- **U5.1** (`8c863bc`) shutdown-save exit code — keep; do not revert.
- No production-hardening (corrupt checkpoint, recovery, disk-full) unless explicitly assigned.
