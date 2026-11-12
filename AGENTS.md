# Agent rules (apptraverse — unified worktree)

## SharedNode task — 2026-11-11

The user explicitly authorized new SharedNode implementation in a separate
branch from main, beginning with native serialization and headless memory
tests. `feature/shared-node-native-memory` owns this opt-in extension; its
contract and remaining stages are in [SharedNode.md](docs/architecture/SharedNode.md).
This authorization supersedes the older SharedNode removal/deferment below
for this extension. Existing WindowCatalog/backend boundaries still apply.
The follow-up authorizes ACK/retry and two live memory replicas, including
loss/duplicate/out-of-order and restart checks; current evidence is in
[live-memory validation](docs/validation/shared-node-live-memory-2026-11-11.md).
The next follow-up authorizes a third participant admitted through H and direct
all-pairs delivery, with Join/snapshot/Event races, losses, Offline and restart
checks, and no incremental replay of snapshot-covered history. Scope and current
evidence are in [three-peer validation](docs/validation/shared-node-three-peer-memory-2026-11-11.md).
The chronological-generation follow-up authorizes per-author/per-Node indexes,
continuous-prefix ACKs and persistent monotonic author timestamps, with memory
loss/reorder/restart checks. That slice left compaction blocked. The indexed contract
and evidence are in [indexed-memory validation](docs/validation/shared-node-indexed-memory-2026-11-11.md).
The checkpoint follow-up supersedes that hold only through the confirmed
SharedNode checkpoint policy: persistent progress promises, exact membership,
all-recipient delivery evidence and memory restart/loss tests. It authorizes
logical journal/origin-payload compaction, not native object-store garbage
collection. See [checkpoint-memory validation](docs/validation/shared-node-checkpoint-memory-2026-11-11.md).
The transport follow-up authorizes the optional Aether adapter using the
presence-on-streaming SDK line (`bench/presence-streaming-recovery`, pinned
committed SDK including its allocation-free budget fix) and headless real-cloud
verification. The next
chat must be the simplest SharedNode demonstration, without localization or
unrelated features; this slice connects the transport first. Current evidence
is in [Aether validation](docs/validation/shared-node-aether-2026-11-11.md).
The current follow-up explicitly requires the client's native presence API
(`Client::IsLocallyOnline` / `Client::QueryPeerPresence`) and the simplest
Windows chat demonstrating SharedNode. It supersedes the adapter probe/echo
policy and the earlier transport-only hold on GUI work. Use an isolated pinned
SDK checkout from the presence experiments, no application echo fallback.
This opt-in demo has fixed English labels, one room, host admission, direct
participant messages and private persistent profiles; unrelated UI remains out
of scope. Profile restore is required for this replication demonstration.
The latest follow-up authorizes combining this native presence API with the
working streaming/push SDK base used by App, preserving its registration and
ordinary message wire format. Validate real registration, native presence and
the minimal chat; do not substitute echo for the native API.
The 2026-11-12 follow-up explicitly disables availability gating for now:
the default Aether link sends without querying presence, and SharedSync keeps
each durable packet until its ACK, retrying at the configured timeout. Native
presence may remain an explicit opt-in; the minimal chat shows participant
names without Online/Offline badges. This supersedes the native-presence
requirement above for the default demo and its cloud acceptance.
The following correction requires event-driven chat publication/presentation:
no periodic ChatView refresh and no UI polling timer. Use the existing Node
materialized-change notifications. Chat state/history and pending receipts
stay in RAM, with no chat disk Save/Load on send/receive/shutdown. This
supersedes demo profile-restore acceptance; SDK identity storage is separate.

- **Never** add an AeroAdmin dependency or copy AeroAdmin sources.
- Windows is the first platform; the common model must stay cross-platform (no HWND, Win32 paths, or Win32 keycodes in `src/common/`).
- Model first: `surfaces_demo` / WindowCatalog semantics — model thread → publication → UI mirror → presenters; GUI → `ModelObjectProxy` → model Events.
- UI mirror and presenters must not mutate persistent model directly.
- Keep presenters thin; one logical control → one presenter responsibility where practical.
- Persistent vs runtime separation is mandatory (HWND, transport handles are runtime-only).
- Windows USER uses ordinary RamDomainStorage over embedded BACKING. Child Close saves only to RAM; application shutdown writes one user.bin. Normal Debug always creates a fresh graph and does not load prior USER. No separate checkpoint sidecar.
- Prefer incremental **Debug** builds; do not clean/rebuild without cause. **No** Release by default, no fresh populate without user request.
- Update [docs/versioning/PROGRESS.md](docs/versioning/PROGRESS.md) only after verified steps. Do not mark PASS without execution.
- Do not merge to `main` or force-push without explicit user instruction (task may specify `--force-with-lease` on `integration/unified-application`).
- Рабочие изменения: commit + push в текущую ветку. PR — только по прямой команде.
- Distilled UI graph contract is mandatory (button, label, locale, window_presenter, links): do not re-validate with optional runtime checks, fallbacks, or silent skips in presenters.
- Runtime lifecycle states (`ReadyForPresentation`, activated parent, disabled control, closed/lazy window) are real; do not conflate them with missing graph links.
- Side effects (Save/Load/Inject, I/O, mutation) must not run only inside `assert`; execute once, store the result, assert on that result (must still run under `NDEBUG`).
- **WindowCatalog prototype schema:** application classes stay **S0-only** for now. New fields go into existing S0 Load/Save; old experimental on-disk profiles are **not** migrated or auto-read. Application S1/S2 or migrations only by separate assignment — this does not remove library versioning or specialized versioning tests.
- Cold persistence tests must pair USER with the same embedded BACKING. Re-running a Debug producer can assign new constant IDs; reusing older USER with that new supply is an unsupported upgrade scenario, not a cold-restart test. Preserve prior profiles instead of adding silent repair/reset paths.

## Porting and regression (backend / template work)

When a defect appears during porting, fix the **architectural cause** in the right layer first. Then re-run existing E/E2E/smoke for **every backend already verified**, in the **same scope** as their historical PASS. Check the **same boundary** and **analogous call sites** for the same defect and broken shared contracts — not a repo-wide audit.

- Fix new blockers for the current scenario and re-verify before claiming PASS.
- Record separate non-blocking findings explicitly; do not mark them fixed.
- Missing toolchain/device → **BLOCKED / NOT RUN**; build alone is not runtime proof.
- Keep historical PASS records with their scope; record current regressions separately.
- Non-blocking Object Core matrices and production hardening stay **DEFERRED**.

Current work: the user authorized merging the completed MacBook branch `feature/apple-window-catalog` into `feature/domain-hierarchy-cloning`, preserving Windows/Linux/Android/Web and the shared application template. The earlier Apple-branch isolation restriction is superseded by this merge request. Validate the integration on available hosts and distinguish upstream Apple evidence from a merged Apple runtime run. CI and other example applications remain excluded. Maximization support is explicitly deferred.

Acceptance checkpoint (2026-11-10): these three ports are implemented and tested within the scopes recorded in `docs/validation/window-catalog-{linux,android,web}-port-2026-11-10.md`. The 2026-11-11 lifecycle follow-up extends Web persistence to hidden/pagehide and verifies ordinary Android task dismissal; use the load-only Debug variants for manual persistence, preserving normal Debug fresh policy. Preserve the documented WSLg position limitation and the Windows interactive-foreground test precondition. ARM64/devices, other browsers, Release packaging and deferred maximization are not implied follow-up tasks.

## Active plan (P0–P6)

- **Order:** integrate the completed Apple WindowCatalog branch and preserve existing platform/headless coverage. [PLAN §P0–P6](docs/versioning/PLAN.md#активный-план-мультиплатформенный-windowcatalog-p0p6) retains the long-term roadmap; its old other-platform NEXT entries and **U5.2–U6**, **9D–9H** are not active instructions.
- **Priority:** WindowCatalog integration across the explicitly authorized ports, Windows and headless. Preserve the common application template; CI, MinimalApp/other examples, remaining platforms, administration and the long-term P3/P4 roadmap require a separate task.
- **Boundary:** common/backend libraries не зависят от examples/WindowCatalog; native host/controls принадлежат backend, модель/Events/prefab/composition — приложению. Независимый MinimalApp проверяет переиспользование. Не копировать SurfacesModelSession и mirror-код в новые порты.
- **Deferred (не возвращать в активную очередь без отдельного задания):** Object Core T16/T17, полная матрица Clone/upgrade, `kSupplyConstantIds` replacement, production hardening, resource download matrices — см. PLAN §P3 DEFERRED.
- **Canon:** [docs/architecture/AppTraverse_Architecture_RU.md](docs/architecture/AppTraverse_Architecture_RU.md) (2026-11-10 accepted principles; historical observations are explicitly separated).
- **SharedNode / replication / Æther network client:** **full removal** (**P1.1**), not OFF or stubs.
- **U5.1** (`8c863bc`) shutdown-save exit code — keep; do not revert.
- No production-hardening (corrupt checkpoint, recovery, disk-full) unless explicitly assigned.
