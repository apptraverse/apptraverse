Status: implemented, verified locally. Not accepted.

# surfaces_demo slice 2 — Windows minimal multi-window

## Starting / final

- Starting HEAD: `ddd85bd`.
- Branch: `prep/deps-objects-assert-mcp-v1`.
- Final SHA: `d8bdb97`.
- Pushed to origin/prep/deps-objects-assert-mcp-v1.

## Cleanup (slice 1 residuals)

Removed redundant checks from `surfaces_model.cpp`:

- Apply Add/Remove: no `is_valid` / `is_loaded` / parent relation re-checks
- `Surface::AddSurface` / `Remove`: no `domain != nullptr`
- `AddClick` / `RemoveClick`: no `model_proxy != nullptr`

Kept: live-membership miss → no-op; Apply Remove miss → assert (broken Event).

## Presenter hierarchy

```
SurfacePresenter
  ↓
DesktopSurfacePresenter
  └─ Win32SurfacePresenter  (HWND hwnd, HWND add_button)
```

Object-system registration; GUI LoadRoot picks Win32 as most-derived.
No `dynamic_cast`; hierarchy proven via `Registry::GenerationDistance`.

## Native paths

- **Add:** BUTTON → `DispatchChildCommand` → `OnCommand` → `AddClick` → proxy →
  `Surface::AddSurface` → Event → structural pub → new `OnLoad` / HWND
- **X non-last:** `WM_CLOSE` → `RemoveClick` → Event → `OnUnload` / DestroyWindow
- **X last:** `WM_CLOSE` → `WM_APPTRAVERSE_STOP` (no Remove); Surface persisted

## HWND ownership

One WNDCLASS for all Surface windows. HWND created only in `OnLoad`, destroyed
only in `OnUnload`. Survivors keep identity across Add/Remove.

## Manual executable

`build/win64-ninja-msvc-debug/examples/surfaces_demo/windows/win32_surfaces_demo.exe`
(`--state-dir <path>`). Load-only: `win32_surfaces_demo_load_only.exe`.

## `/GR-` proof

Ninja FLAGS for desktop/win presenters, WinApp, smoke test include `/GR-`.

## Tests

PASS: `apptraverse_surfaces_model_test`, `apptraverse_surfaces_win32_smoke_test`
(add from any window, middle close, last-window exit + restart Surface 3).
Regressions PASS: dynamic_objects_add, presenter_load_order.

Not accepted-by-user.

---

Status: implemented, verified locally. Not accepted.

# surfaces_demo slice 1 — common model + headless

## Starting / final

- Starting HEAD: `f6bbdc8`.
- Branch: `prep/deps-objects-assert-mcp-v1`.
- Final SHA: `e0b33a8`.
- Pushed to `origin/prep/deps-objects-assert-mcp-v1`.

## Object graph

```
Application
 └── surfaces → Surfaces : Node
      └── Surface : Node
           └── presenter → SurfacePresenter
```

Initial distill: Application → Surfaces → Surface #1 → SurfacePresenter.

## Event paths

- `Surface::AddSurface()` → create Surface + Presenter → `InitializeRuntimeNode` → `AddSurfaceEvent` → `Surfaces::Commit`
- `Surface::Remove()` → live-membership check → `RemoveSurfaceEvent` → `Surfaces::Commit` (stale = no-op)
- Topology vector mutates only in `Surfaces::Apply(Add|Remove)`

## Dynamic Node init

Runtime Surface uses canonical `InitializeRuntimeNode` (same as distill `FinalizeDistilledGraph`) before Commit. No second Node-init mechanism.

## Remove current (no model current)

`SurfacePresenter::RemoveClick()` proxies that Surface ObjId. Presentation (future pager / window X) selects which presenter; model has no `current_surface`.

## Proven headless

- initial graph; Add from Surface1 and from Surface2; Remove; replay [1,3]; restart + Add→4
- dynamic Node structural publication (GUI Surface2 new ObjId match, different C++ instance, OnLoad once)
- GUI proxy AddClick / RemoveClick
- presenter lifecycle (no reload of survivors; historical not reactivated)
- shutdown drain of accepted Adds
- `/GR-` on `surfaces_model.cpp` and `surfaces_model_test.cpp`

## Tests

PASS: `apptraverse_surfaces_model_test`
Regressions PASS: dynamic_objects_add, presenter_load_order, publication_channel, event_sourced_core, journal_retention, main_window lifecycle/window.

Windows UI not started.

Not accepted-by-user.

---

Status: implemented, verified locally. Not accepted.

# Disable RTTI and enforce AppTraverse invariants (before surfaces_demo)

## Starting / final

- Starting HEAD: `42536ac`.
- Branch: `prep/deps-objects-assert-mcp-v1`.
- Final SHA: `baa5ed9`.
- Pushed to origin/prep/deps-objects-assert-mcp-v1.

## Permanent rules

Updated `.cursor/rules/apptraverse-coding-agent.mdc`:

- **No C++ RTTI** (`dynamic_cast` / `typeid` / `std::type_info` / `std::type_index` forbidden)
- Use Æther class IDs, `Registry::GenerationDistance`, typed `Ptr`/`ObjPtr`, `Ptr::as<T>()` when type is guaranteed
- **Do not re-check established invariants**
- **No defensive programming** (invariant-driven; applies to all future ports)

## Build policy

- `cmake/apptraverse_compile_policy.cmake` → INTERFACE `apptraverse_compile_policy`
  - MSVC: `/GR-`
  - GCC/Clang: `-fno-rtti`
- `apptraverse` PUBLIC-links the policy (consumers inherit automatically)
- Targets that do not link `apptraverse` link the policy PRIVATE
  (`publication_channel_test`, `win32_fatal_ndebug_*`)

## Compile-time guard

- `include/apptraverse/no_rtti.h` (`_CPPRTTI` / `__GXX_RTTI` → `#error`)
- Included from `include/apptraverse/object_macros.h` (central AppTraverse object header)

## Removed `dynamic_cast` (AppTraverse-owned)

Replaced with `Registry::GenerationDistance` + `static_cast`, or typed `ObjPtr`/`Ptr`:

| location | replacement |
| --- | --- |
| `src/object_serialization.cpp` | local `AsObjOf<T>` / `AsPresenter` |
| `src/graph_mirror.cpp` | `GenerationDistance` + `static_cast` for Node |
| `examples/dynamic_objects_demo/common/dynamic_lifecycle.cpp` | structural apply cleanup + no Session repair |
| `examples/chat_ui_runtime_demo/common/chat_shared.cpp` | class-id / typed path |
| `tests/chat_p2p_headless_test.cpp` | class-id / typed path |
| `tests/chat_presentation_headless_test.cpp` | class-id / typed path |
| `tests/dynamic_two_main_win32_test.cpp` | `GetClassId` + typed presenter |
| `tests/journal_retention_test.cpp` | typed path |
| `tests/main_window_window_changed_test.cpp` | typed path |
| `tests/shared_journal_test.cpp` | typed path |

`git grep dynamic_cast` in `*.cpp`/`*.h`: only the intentional string CHECK in
`tests/dynamic_objects_add_test.cpp` (WndProc source must not contain the token).

No `typeid` / `std::type_info` / `std::type_index` in AppTraverse-owned source/tests.

## Defensive-check cleanup

- `ApplyItemListStructural`: call `OnModelChanged()` on live presenters directly
  (no `is_valid` / `is_loaded` / `presentation_loaded` gates after structural apply)
- `EnsureItemListWindowLink` **removed**: Session must not repair graph schema.
  `ItemList::window` is schema v1; pre-v1 persisted state requires re-distill /
  fresh state (development demo policy).
- Left real alternatives: WndProc `presenter == nullptr` before userdata attach;
  `CreateWindowExW` failure → `FatalWin32`; event Apply asserts at Commit boundary.
- Removed unused `#include "aether/clock.h"` from `event_sourced_core_test.cpp`
  (test links only `apptraverse`, not full Aether client).

## `/GR-` proof (MSVC incremental `build/win64-ninja-msvc-debug`)

Actual ninja `FLAGS` for AppTraverse-owned CXX objects include `/GR-` and not bare `/GR`.
Examples after regenerate:

```
object_serialization.cpp.obj ... /Zc:preprocessor /GR-
publication_channel_test.cpp.obj ... -std:c++20 -MDd /GR-
win32_fatal_ndebug_child.cpp.obj ... -std:c++20 -MDd /GR-
```

Verbose `cl.exe` lines for AppTraverse TUs also showed `/GR-`.

GCC/Clang policy is wired as `-fno-rtti` (not exercised on this Windows slice).

## plan.md

Next: **surfaces_demo common model + headless only**.
Then Windows minimal multi-window (no resize/Z-order/DPI), then macOS/Linux,
then mobile, then SharedNode / Chat / AeroAdmin-X.
`surfaces_demo` **not started** in this slice.

## Tests (local incremental)

PASS:

- `apptraverse_presenter_load_order_test`
- `apptraverse_dynamic_objects_add_test`
- `apptraverse_dynamic_two_main_win32_test`
- `apptraverse_publication_channel_test`
- `apptraverse_main_window_window_changed_test`
- `apptraverse_main_window_lifecycle_test`
- `apptraverse_main_window_win32_smoke_test`
- `apptraverse_dynamic_objects_win32_smoke_test`
- `apptraverse_event_sourced_core_test`
- `apptraverse_journal_retention_test`

MCP: first build attempt failed on a stale target list mid-reconfigure; local
MSVC/ninja incremental script completed successfully. Chat headless suite not
re-run as primary gate this slice (casts removed; core/dynamic/main_window set above).

Not accepted-by-user.

---

Status: implemented, verified locally. Not accepted.

# dynamic_objects_demo final cleanup (before surfaces_demo)

## Starting / final

- Starting HEAD: `4e6098f`.
- Branch: `prep/deps-objects-assert-mcp-v1`.
- Final SHA: `5042132`.
- Pushed to `origin/prep/deps-objects-assert-mcp-v1`.

## Changes

- `ReadyForPresentation` only checks parent `presentation_loaded`.
- After multipass `InitializeNewPresenters`, every live Presenter must be
  `presentation_loaded` (assert; broken graph is not "not ready").
- No `dynamic_cast` in dynamic demo / ModelObjectProxy / presenter-walk path.
  Typed conversion: `ae::Ptr::as<T>()`, `ObjPtr` AbleToCast conversion,
  `Registry::GenerationDistance` for Presenter discovery (`AsPresenter`).
- `Presenter::OnCommand` + Win32 `DispatchChildCommand`; parents do not know
  Add/Remove. (Separate `Win32Presenter` Obj base avoided — would diamond with
  `MainWindowPresenter` / `AddItemPresenter`.)
- `WM_APPTRAVERSE_CLOSE_WINDOW` → `RequestStop()` directly.
- `Item::Remove`: required `list` relation; only live-membership miss is no-op.

## Tests (local incremental `build/win64-ninja-msvc-debug`)

PASS: dynamic add/proxy/Ready/WndProc-source, Win32 smoke, two-main,
presenter load order, publication_channel, main_window lifecycle/window/smoke.

Not started: surfaces_demo.

Not accepted-by-user.

# UI ownership + ModelObjectProxy (before surfaces_demo)

## Starting / final

- Starting HEAD: `67fb538` (foundation hardening).
- Branch: `prep/deps-objects-assert-mcp-v1`.
- Final SHA: `4327336`.
- Pushed to `origin/prep/deps-objects-assert-mcp-v1`.

## Object graph

```
Application
 └── MainWindow
      ├── presenter → MainWindowPresenter (top-level HWND only)
      ├── add_item → AddItem
      │                └── presenter → AddItemPresenter (Win32: BUTTON HWND)
      └── item_list → ItemList
                       ├── presenter → ItemListPresenter
                       └── Item → ItemPresenter (label + [x] HWNDs)
```

Presenter does not own child presenters; composition is the object graph.

## GUI → model path

```
Win32 click → Win32*Presenter → AddItemPresenter::Click / ItemPresenter::RemoveClick
  → ModelObjectProxy::Invoke(ObjId, &T::method)
  → session.Post(ModelWork)
  → model Domain::Find(ObjId) → AddItem::Click / Item::Remove
  → AddItemEvent / RemoveItemEvent → ItemList::Commit
  → NoteMaterializedChange → structural publication → GUI mirror
```

- No model pointers across domains/threads.
- GUI Presenter never Commits Events.
- Session/WinApp no longer know Add/Remove semantics (`AddItemCommand`,
  `RemoveItemCommand`, `WM_APPTRAVERSE_ADD/REMOVE_ITEM`, free `CommitAddItem` /
  `CommitRemoveItem` removed).
- Shutdown drain preserved on generic `pending_work` (accepted before stop runs;
  after stop rejected).

## Tests (local incremental `build/win64-ninja-msvc-debug`)

| check | result |
| --- | --- |
| `apptraverse_dynamic_objects_add_test` (graph, Click/Remove, GUI proxy, drain) | PASS |
| `apptraverse_dynamic_objects_win32_smoke_test` (Add BUTTON owned by Win32AddItemPresenter) | PASS |
| `apptraverse_dynamic_two_main_win32_test` | PASS |
| `apptraverse_presenter_load_order_test` | PASS |
| `publication_channel` + `main_window_*` lifecycle/window/smoke | PASS |

MCP `apptraverse_build_start` hit a transient `unknown target` against this tree;
proof is the local MSVC+ninja incremental script.

Not started: surfaces_demo.

Not accepted-by-user.

# Foundation hardening for surfaces_demo

## Starting point

Branch `prep/deps-objects-assert-mcp-v1` at `6a63fd6` (Remove Item Progress SHA).

## Explicitly NOT in scope

Storage Save/Load / DirectoryDomainStorage / filesystem failures are assumed
impossible in this architecture. No error propagation work.

SharedNode, transport, presence, chat, surfaces_demo implementation, structural
delta/bandwidth optimization — not started. TODO recorded for structural payload
size only.

## Issues fixed

1. **Presenter load order** — runtime `presentation_load_order` set on successful
   OnLoad; UnloadPresenters / structural unload sort descending (child before parent),
   independent of ObjId / Save collect order. Win32 no longer uses IsWindow
   “parent already destroyed child” branches.

2. **Explicit parent** — `ItemList::window` (`ae::ObjPtr<MainWindow>`); graph sets
   both directions. Win32 ItemList uses `list->window`, not fixed MainWindow ObjId.
   v0 saves migrate via `EnsureItemListWindowLink`.

3. **Native class lifetime** — `RegisterDynamicWin32Classes` /
   `UnregisterDynamicWin32Classes` around WinApp; per-HWND OnLoad/OnUnload only
   create/destroy windows.

4. **WM_CLOSE** — presenter posts `WM_APPTRAVERSE_CLOSE_WINDOW(window ObjId)`;
   WinApp stops only when that id is the single MainWindow.

5. **Generic structural keepalive** —
   `CaptureStructuralPresentationKeepalive` / `ApplyStructuralPublicationAndUpdatePresenters`
   hold `ae::Ptr<ae::Obj>` + `Presenter::ptr`. `ApplyItemListStructural` no longer
   enumerates concrete Item/presenter vectors for lifetime.

6. **Shutdown drain** — after `RequestStop`, accepted deque commands Commit in order
   without requiring GUI publication consumption; then Save. Submit after stop is no-op.

7. **Distill separation** — `dynamic_distill.cpp` / `BuildDynamicObjectsGraph` linked
   only by distill demo + fixture tests; load-only does not link it.

8. **CMake** — `APPTRAVERSE_BUILD_AETHER_DEMOS` (default ON) gates full `aether` client
   + chat/model_ui/presence. Core + dynamic_objects + main_window build without it.
   `NOMINMAX` / `WIN32_LEAN_AND_MEAN` only `if(WIN32)`.

## Tests (rebuilt/relinked/run locally)

| check | result |
| --- | --- |
| `apptraverse_presenter_load_order_test` | PASS |
| `apptraverse_dynamic_objects_add_test` (incl. shutdown drain) | PASS |
| `apptraverse_dynamic_two_main_win32_test` | PASS |
| `apptraverse_dynamic_objects_win32_smoke_test` | PASS |
| `publication_channel_test`, `main_window_*` lifecycle/window/smoke | PASS |

MCP used only for an early configure probe (`already_configured`); proof is local
incremental `build/win64-ninja-msvc-debug` with MSVC env.

Commits: `7fab099` (presenter lifecycle / structural keepalive), `2ce939a`
(Win32 multi-window prep), `52c386f` (shutdown drain / distill / CMake / plan).
Pushed to `origin/prep/deps-objects-assert-mcp-v1`.

Not implemented: surfaces_demo.

Not accepted-by-user.

# Dynamic objects demo — Remove Item

## Starting point

Branch `prep/deps-objects-assert-mcp-v1` at `ef26be6` (Add Item Progress SHA note).

## Representations

- `RemoveItemCommand { ae::ObjId item_id }` — copyable identity only; no model pointer.
- `RemoveItemEvent : EventFor<ItemList,…> { Item::ptr item }` — Apply erases that Item from live `items` and `NoteMaterializedChange()`. No physical destroy, no journal GC, no presenter/Win32 calls.
- Command queue: `std::variant<AddItemCommand, RemoveItemCommand>` deque (discrete; not coalesced).

## Live vs historical

Removing from `ItemList::items` does not remove historical reachability via `AddItemEvent` in the journal. GUI presentation walks **live** topology only (`CollectLiveReachableObjects` temporarily clears Node `base`/`journal` for Save-based collect). Historical Item/presenter may remain graph-reachable but must not re-`OnLoad`.

## Presenter unload

- Capture active presenters before structural apply (held via `Presenter::ptr`).
- Apply structural publication fully.
- `UpdatePresentersAfterStructuralPublication`: OnUnload presenters in previously-active minus live; then `InitializeNewPresenters` for newly live only.
- `UnloadPresenters` iterates reverse collect order so child HWNDs go before parents; child OnUnload tolerates already-destroyed HWND if parent won the race.
- Win32 Item `[x]` → `WM_APPTRAVERSE_REMOVE_ITEM` → command → Event → publication → OnUnload → `DestroyWindow` row/button.

## Numbering

`CommitAddItem` uses `max(existing.number)+1` (not `size()+1`). After remove Item2, next Add is Item4.

## Stale / double Remove

`CommitRemoveItem`: if ObjId not in live `items`, **no-op** (`return false`, no Event). Chosen because GUI can race a second click; must not delete another Item or abort.

## Persistence

No Save on Remove. Shutdown `Application::Save()` after model stop. ItemList retention unchanged (unlimited). Empty live list is valid.

## Tests

| check | result |
| --- | --- |
| model remove + journal still holds historical Item | PASS |
| replay Add+Remove → live list without Item2 | PASS |
| mirror remove: survivor pointer identity, OnUnload==1 | PASS |
| historical presenter: no second OnLoad after remove | PASS |
| middle remove + Add → numbers 1,3,4 | PASS |
| sequence Add/Add/Remove/Add/Remove → live 3,4 | PASS |
| Win32 smoke Add/Remove + child load-only restore Item3,Item4 | PASS |
| `publication_channel_test`, `main_window_window_changed_test` | PASS |
| `main_window_lifecycle_test`, `main_window_win32_smoke_test` | PASS (existing bins) |

MCP `user-apptraverse` not used as proof for this slice (local incremental `build/win64-ninja-msvc-debug`).

Commits: `19c31a9` (presenter unload / live topology collect), `1c24de2` (dynamic_objects_demo Remove Item). Pushed to `origin/prep/deps-objects-assert-mcp-v1`.

Not implemented: SharedNode / transport / presence / chat / GC.

Not accepted-by-user.

# Dynamic objects demo — Add Item

## Starting point

Branch `prep/deps-objects-assert-mcp-v1` at `460ee2a` (journal retention/compaction predecessor).

## What landed

New example `examples/dynamic_objects_demo`:

```
Application → MainWindow → ItemList → Item(s)
```

- Initial graph: one `Item` (`number=1`).
- GUI Add → `AddItemCommand` (deque, not coalesced) → model `CommitAddItem`.
- `AddItemEvent` carries pre-created `Item::ptr` so replay keeps the same ObjId (`Apply` only `push_back`).
- ItemList keeps default unlimited journal retention (AddItemEvent survives restart/replay).
- Shutdown: `Application::Save()` only; no Save on Add.

### Structural publication (generic)

- `SerializeStructuralNodePublication` / `ApplyStructuralPublication`: envelope like incremental node pub, payload is `SerializeObjectGraphToBuffer` so newly referenced Item/presenter layers enter the GUI Domain.
- Field-only MainWindow updates still use `SerializeIncrementalNodePublication`.

### Presenter activation (generic)

- `Presenter::presentation_loaded`, `ReadyForPresentation()`.
- `InitializeNewPresenters` multipass: only not-yet-loaded + ready presenters get `OnLoad`.
- `InitializePresenters` delegates to it. Existing presenters are not re-OnLoad'd after Add.

### ObjId path

```
model Create Item (GenerateUnique)
→ AddItemEvent.item
→ Commit/Apply (topology)
→ SerializeStructuralNodePublication(ItemList)
→ GUI ApplyStructuralPublication (LoadRoot/shells for new ids)
→ InitializeNewPresenters → Win32ItemPresenter::OnLoad (new STATIC row)
```

Mirror Item ObjId == model Item ObjId; C++ pointers differ. Application/MainWindow/ItemList/old Item/old presenters preserve identity.

## Tests

| check | result |
| --- | --- |
| `apptraverse_dynamic_objects_add_test` (model add, replay identity, structural pub + OnLoad counts, two Adds, restart) | PASS |
| `apptraverse_publication_channel_test` | PASS |
| `apptraverse_main_window_window_changed_test` | PASS |
| `apptraverse_dynamic_objects_win32_smoke_test` (in-process Add row; child distill Add then load-only restore) | PASS |

MCP `user-apptraverse` build against this checkout: failed with `unknown target` before local reconfigure of new targets; not used as proof. Local incremental build in `build/win64-ninja-msvc-debug`.

Commits: `629162f` (structural publication / InitializeNewPresenters), `6102c03` (dynamic_objects_demo Add Item). Pushed to `origin/prep/deps-objects-assert-mcp-v1`.

Not implemented: Delete/Remove Item. Next slice after Add verification.

Not accepted-by-user.

# Dynamic Node journal retention / compaction

## Problem

Live MainWindow state accumulated 533 `WindowChangedEvent` objects. Startup replayed all of them; shutdown rewrote the whole graph. Resize itself was already RAM-only; the journal was unbounded.

## Policy API

- `JournalRetentionPolicy`: `max_events` (`0` = retain none; `kUnlimitedEvents` = unlimited count) and optional `max_age` (inclusive `now_us - retained_since_us <= max_age`).
- Default policy: unlimited (Chat/Shared unchanged).
- `SetJournalCompactionBlocked` / `IsJournalCompactionBlocked`: synchronization hold, independent of retention.
- `EventRecord::retained_since_us`: local replica acceptance time; not order/id. `Commit` / shared insert stamps `SystemUtcMicros()`. Lamport is not used as age.
- Schema: Node journal wire v2 includes `retained_since_us`. Node `Load(Version<1>)` migrates by stamping load time (conservative). MainWindow bumped to v4 (`Node` Save/Load v2); v3 Load still migrates old resize states.

## Compaction

`CompactJournal(now_us)` collapses only a contiguous unsafe prefix into `base` via existing `RebuildFromBaseAndReplay` + `CaptureBaseStateInto`, keeps retained suffix, restores Generation, suppresses materialized-change notifications.

When the retained suffix is empty (MainWindow `max_events=0`), compaction clears the journal and snapshots already-materialized live fields into `base` without reloading the old base into the live object.

MainWindow sets `max_events=0` after load; shutdown: `CompactJournal` then `Application::Save()`.

## Proof

- Count 100→10; age boundary inclusive; count+age union (3+age→7, 10+age→10); blocked then unblock; dynamic policy; shared order preserved; mid-insert while blocked; 500→0 reachable events; retention=10 reachable=10.
- Node journal v1 (`LegacyRetentionDoc`) save → load stamps `retained_since_us` → compact → reload.
- Unreferenced event dirs may remain after SaveRoot; reload does not load them.
- MainWindow 50 session commits → journal 0 / reachable WindowChangedEvent 0; direct 500 WindowChangedEvent compact → 0, then retention=10 → 10.

## Filesystem orphans

`DirectoryDomainStorage` SaveRoot does not delete previously stored event directories. Startup does not load unreferenced events. Safe GC of orphan event dirs is a separate TODO.

## Manual

Prior interactive resize verification still stands. Compaction-on-shutdown of a large existing temp state: next graceful close will compact; not re-measured by the agent. Not marked accepted.

## Tests actually run

Cursor `user-apptraverse` MCP is not bound to this checkout (`source_dir` missing). Local incremental `cmake --build --preset win64-ninja-msvc-debug`.

| target | artifact | status |
| --- | --- | --- |
| journal retention + core + shared | local cmake | ok (`journal_retention_test`, `event_sourced_core_test`, `shared_journal_test`) |
| main-window headless | local cmake | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_window_changed_test`, `main_window_missing_load_test`) |
| Win32 smoke | local cmake | ok (`main_window_win32_smoke_test`) |

`CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
Build dir: `build/win64-ninja-msvc-debug` (incremental; no clean)

Incidental: `cmake/aether_object.cmake` falls back to `CPM_PACKAGE_libbcrypt_SOURCE_DIR` when `libbcrypt_SOURCE_DIR` is unset.

Not accepted-by-user.

# Persist model state only on shutdown

Runtime resize no longer calls `main_window.Save()`. `WindowChangedEvent` / `Commit` / journal / generation stay in memory. Incremental publication still uses `SerializeObjectToBuffer` (`RamDomainStorage` scratch) and does not touch `DirectoryDomainStorage`.

After `RequestStop`, before the model `Application` / Domain / storage leave scope, `Application::ptr::Save()` writes the live graph once (`DomainGraph::SaveRoot`). Not distillation. `Run` returns only after that save, so Windows `SetEvent` still means persistence shutdown finished.

Disk geometry stays the loaded snapshot until that save. A crash before graceful model shutdown may lose runtime commits. Sequence/ack fields stay unserialized.

## Proof

- Coalesce A/B/C: disk remains default until `RequestStop`; after join, geometry C, journal size 0 (post-compaction).
- Three consumed publications A then B then C: filesystem snapshot unchanged between commits; after join, geometry C, journal size 0.
- No-op seq 7: snapshot unchanged before stop; after join, journal size 0.

## Manual

Sequence/ack interactive resize: successful user verification (right/left drag no longer rolls back). Not marked accepted.

MANUAL interactive drag after removing runtime Save — not re-run by the agent. Disk writes during drag: none by test, not a measured latency number.

## Tests actually run

Cursor `user-apptraverse` MCP is not bound to this checkout. Local incremental `cmake --build --preset win64-ninja-msvc-debug`.

| target | artifact | status |
| --- | --- | --- |
| headless + Win32 smoke + demos | local `cmake --build --preset win64-ninja-msvc-debug` (no MCP artifact) | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_window_changed_test`, `main_window_missing_load_test`, `main_window_win32_smoke_test`) |

`CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
Build dir: `build/win64-ninja-msvc-debug` (incremental; no clean)

Not accepted-by-user.

# Stale model feedback during live resize

## Failing interleaving

Publication of geometry A can arrive after the user has already moved the HWND to B. `OnModelChanged` compared actual B with desired A and `SetWindowPos(A)`, which echoed A back as a native command and replaced the newer pending B. Left-edge drags jumped because the stale publication also restored x.

## Fix

`WindowChangedCommand` carries a GUI-side monotonic `sequence`. Latest-state coalescing keeps the newest sequence. Every taken command, including a geometry no-op, publishes `processed_window_change_sequence` ahead of the existing MainWindow incremental payload. The GUI mirror always applies the model state. `OnModelChanged` drives the HWND only when `ack >= last_submitted`. No suppression flag.

Creation-time `WM_WINDOWPOSCHANGED` is not user input: presenter userdata is attached only after `ShowWindow`/`UpdateWindow`. Teardown clears userdata before `DestroyWindow` instead of nulling `hwnd`.

PublicationChannel remains single-unread. The model cannot publish the next buffer until `TakePublishedCopy` releases the slot, so one notify message still matches one buffer. Sequence is application protocol, not a channel field.

## Counts from tests

- Coalesce seq 1/2/3 before processing: 3 commands, 1 committed `WindowChangedEvent`, 1 incremental publication, journal size 1, ack=3.
- No-op seq 7: journal unchanged, generation unchanged, no Save, ACK publication received, `publish_count` 1 → 2.
- Stale A then B: mirror becomes A while native-emulation stays B; then ack 2 applies B. Same Application/MainWindow/Presenter. `OnLoad` once. Journal size 2.

## Tests actually run (local runner, not attached MCP)

Verified tree before this fix: `84c23688319bf5425e5f51ad983f68066a0d0c3c`.
Commit / push: `f3a74c53bc3531920bb3a0937615da676272db83` on `origin/prep/deps-objects-assert-mcp-v1`.

Cursor `user-apptraverse` MCP is not bound to this checkout (`source_dir` missing). This turn's MCP `apptraverse_build_start` (`20260909-044756-5b4ff3`) failed immediately with `unknown target 'apptraverse_main_window_headless_check'` and did not write an artifact here. Local incremental `cmake --build --preset win64-ninja-msvc-debug` (same targets, MSVC env) is the verification.

MANUAL INTERACTIVE DRAG — BLOCKED / requires user verification. Automated `SetWindowPos` burst (including left+width) is not a mouse-drag proof. Do not treat jitter as fixed from unit tests alone.

| target | artifact | status |
| --- | --- | --- |
| headless + Win32 smoke + demos | local `cmake --build --preset win64-ninja-msvc-debug` (no MCP artifact) | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_window_changed_test`, `main_window_missing_load_test`, `main_window_win32_smoke_test` including left-edge burst then load-only C) |

`CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
Build dir: `build/win64-ninja-msvc-debug` (incremental; no clean)

Not accepted-by-user.

# WindowChanged round-trip

## Finding

A most-derived factory load does not walk ancestor layers; that was already fixed in aether-objects. This iteration adds the first functional event path on top of that load.

Derived `load` still does not replace App Traverse command/event semantics. Native geometry must be copied into a plain command, committed as `WindowChangedEvent` on the model thread, and published as one changed MainWindow.

## Semantics

- `WindowChangedCommand` (`main_window_lifecycle.h`): four `int32_t`, latest-state under `ModelSession::mu`. Newer geometry replaces an older pending command.
- Model waits on `stop` or (pending command and publication slot free). Stop wins if both are ready.
- Unchanged geometry does not commit an event or change generation. Later resize-ack iteration still publishes the processed sequence (see above).
- `WindowChangedEvent` (`main_window_model.h`) is committed on `MainWindow`. Journal gets one event per applied command. `MainWindow::Save()` persists it.
- Incremental envelope: object id, generation, payload length, `SerializeObjectToBuffer` payload. GUI mirror journal/base stay empty. Generation is adopted.
- Existing presenter is held across deserialize so the native instance is not replaced.
- Publication backpressure is unchanged: unread publication is not overwritten. GUI `TakePublishedCopy` under `session.mu`, then `cv.notify_all()`, then apply.
- `PublicationKind::{Initial,Incremental}` chooses the Win32 notify message. GUI does not guess from bytes.
- Win32 source is `WM_WINDOWPOSCHANGED` + `GetWindowRect`. Feedback stops by comparing geometry. No suppression flag.
- aether-objects pin unchanged: `81d5f86f3d6184f86763b4556334dda9471bfd4a`

## Coalescing / no-op / restart

- Coalescing test journal size: 1 `WindowChangedEvent` (commands A/B/C collapsed to C).
- No-op command does not change generation or persist a new `EventRecord`.
- Restart load-only initial publication restores the committed geometry.

## Tests actually run (local runner, not attached MCP)

Cursor `user-apptraverse` MCP still has no `source_dir` (**BLOCKED**).

| target | artifact | status |
| --- | --- | --- |
| headless + demos | `apptraverse-build/20260909-042307-cd233d` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_window_changed_test`, `main_window_missing_load_test`) |
| Win32 smoke | `apptraverse-build/20260909-042240-bce900` | ok (`main_window_win32_smoke_test`, including resize then load-only restored rect) |

`CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
Build dir: `build/win64-ninja-msvc-debug` (incremental; no clean)

Not accepted-by-user.

# Load persisted ancestor layers into aether-objects

## Finding

Derived factory `load` is `DomainGraph::Load<T>` for that class only. `LoadVersion` returns immediately when that class has no stored bytes, so it never walks `AE_REF_BASE`. `GetMostRelatedFactory` already filters unknown classes and sorts the known stored chain base→derived, then creates a further registered descendant. Ancestor state was missing because only that descendant's factory `load` ran. The fix belongs in `DomainGraph::LoadRoot` / `LoadCopyImpl`, reusing that chain.

## aether-objects

- Branch: `fix/load-ancestor-layers-v1`
- Base: `68df7973014fdd366875b3af725a69750a847e8b`
- Final / remote: `81d5f86f3d6184f86763b4556334dda9471bfd4a`
- `LoadRoot` and `LoadCopyImpl` now load known stored class layers base → derived
- Unknown classes stay filtered. Version handling inside a class layer is unchanged
- Tests: `test-object-system` (including new ancestor-layer cases and existing version tests) exit 0

## AppTraverse

- Branch: `prep/deps-objects-assert-mcp-v1`
- Pin: `68df7973014fdd366875b3af725a69750a847e8b` → `81d5f86f3d6184f86763b4556334dda9471bfd4a`
- Configure log: `APPTRAVERSE_aether-objects_SHA=81d5f86f3d6184f86763b4556334dda9471bfd4a`
- Removed `LoadStoredAncestorLayers` and `LoadStoredAncestorLayersFromRoot` (declarations, definitions, model and GUI calls)
- `plan.md` TODO closed
- Commit: `688cfb7b0796d37ecdef0fae8855a1a4e0dea644`

## Tests actually run (local runner, not attached MCP)

Cursor `user-apptraverse` MCP still has no `source_dir` (**BLOCKED**).

| target | artifact | status |
| --- | --- | --- |
| demos + headless + Win32 smoke | `apptraverse-build/20260909-035302-dfe39b` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_missing_load_test`, `main_window_win32_smoke_test`) |

`CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
Build dir: `build/win64-ninja-msvc-debug` (incremental; no clean)

Not accepted-by-user.

# Model lifecycle without Win32 notifications; Win32 fatal helper

## Identity

- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree / source: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Base SHA: `a0629ab9d992aafff9ad55803ffa8bdb93409f2d`
- `CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
- Build dir: `build/win64-ninja-msvc-debug`
- Profile: `win64-ninja-msvc-debug` (incremental; no clean/rebuild)

## ModelSession API before / after

Before:

- `state_dir`, `PublicationChannel`, `mu`/`cv`, `stop`, `RequestStop`, `Run()`
- `notify_hwnd` / `done_event` on the shared session
- `windows.h` / `_WIN32` / `PostMessageW` / `SetEvent` in `main_window_lifecycle.cpp`

After:

- only `state_dir`, `PublicationChannel`, `mu`, `cv`, `stop`, `RequestStop`, `Run(std::function<void()> on_published)`
- no `HWND`/`HANDLE`/`void*`/`uintptr_t` platform stand-ins
- `on_published` is required (no default, no `if (callback)`); called on the model thread after publish, without holding `mu`
- return from `Run` means Application, reachable graph, Domain, and storage are already destroyed on the model thread

## Where Win32 notification and completion live

- `WM_APPTRAVERSE_PUBLISHED` / `WM_APPTRAVERSE_STOP`: `windows/main_window_win32_messages.h` (presenter does not include WinApp)
- notify HWND: `WinApp` (`notify_`), created before the model thread, alive until join
- completion event: local `HANDLE` in `WinApp::Run`, created before the thread, closed after join
- publication: short lambda captures notify HWND and calls `PostMessageW`; fatal via `FatalWin32` on failure
- `SetEvent(done_event)` is in the Windows thread lambda after `Run` returns, not in `ModelSession`

Order: model destruction (scope end) → `Run` returns → `SetEvent` → GUI sees completion → one `join` → `UnloadPresenters` → GUI graph/Domain destroyed → DestroyWindow notify / UnregisterClass / CloseHandle.

## Mutex / cv protocol (unchanged)

- `RequestStop`: lock `mu`, set `stop`, unlock, `cv.notify_all()`
- serialize outside `mu`; under `mu`: `NotePublished` + `PublishProducer`; unlock; `cv.notify_all()`; then `on_published()`
- model wait: predicate `stop` under the same `mu`
- `stop` remains a plain `bool`, not atomic

## Diagnostics

- `FatalWin32(operation, DWORD)` in the Windows example; `GetLastError` captured at the failing call
- teardown result checks: `DestroyWindow` Main/Loading/notify, `CloseHandle(done_event)`; DestroyWindow Main failure does not continue to `UnregisterClassW`
- `WriteFatalStderr`: one write — CRT stderr when attached, otherwise `STD_ERROR_HANDLE`. No unconditional dual print.

## What NDEBUG actually covers

- `apptraverse_main_window_missing_load_test` is a **Debug** binary (not NDEBUG). It checks missing Application: non-zero exit, `fatal: LoadApplication failed` once, publication callback not reached. It does not prove Release/NDEBUG of the application.
- `apptraverse_win32_fatal_ndebug_child` is compiled with `-DNDEBUG`, links only `FatalWin32` + `WriteFatalStderr`, no aether-objects. Parent `apptraverse_win32_fatal_ndebug_test` checks `fatal: RegisterClassW Main GetLastError=5` once, non-zero exit, no execution after fatal. That proves the helper, not the whole app. No full Release rebuild of dependencies.

## Tests actually run (this checkout, local runner)

Cursor `user-apptraverse` MCP schema still has no `source_dir` (**BLOCKED**). Results below are the local `tools/runners/run_apptraverse_build.py` runner, not attached MCP stdio.

| target | artifact | status |
| --- | --- | --- |
| demos + `apptraverse_main_window_headless_check` | `apptraverse-build/20260909-013026-1737b7` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_missing_load_test`) |
| `apptraverse_main_window_win32_smoke_check` + `apptraverse_win32_fatal_ndebug_check` | `apptraverse-build/20260909-013107-b228ae` | ok (`win32_fatal_ndebug_test OK`, `main_window_win32_smoke_test OK`) |

Exes (this build tree, not a neighboring checkout):

- `build/win64-ninja-msvc-debug/tests/apptraverse_main_window_lifecycle_test.exe`
- `build/win64-ninja-msvc-debug/tests/apptraverse_main_window_lifecycle_load_only_test.exe`
- `build/win64-ninja-msvc-debug/tests/apptraverse_main_window_missing_load_test.exe`
- `build/win64-ninja-msvc-debug/tests/apptraverse_win32_fatal_ndebug_child.exe` (`DEFINES = -DNDEBUG`)
- `build/win64-ninja-msvc-debug/tests/apptraverse_win32_fatal_ndebug_test.exe`
- `build/win64-ninja-msvc-debug/tests/apptraverse_main_window_win32_smoke_test.exe`
- `build/win64-ninja-msvc-debug/examples/main_window_runtime_demo/windows/win32_main_window_runtime_demo.exe`
- `build/win64-ninja-msvc-debug/examples/main_window_runtime_demo/windows/win32_main_window_runtime_demo_load_only.exe`

## Limitations / TODO

- `LoadStoredAncestorLayers` still in App Traverse (`plan.md`)
- Do not restore close-during-Loading
- MCP attached tools remain BLOCKED (no `source_dir`)
- Not accepted-by-user

# Main-window startup registration cleanup — progress

## Identity

- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Status: implemented, verified. Not accepted.

## Registration before / after

Before:

- `main.cpp`: `EnsureMainWindowRegistration()` then `EnsureWin32MainWindowPresenterRegistration()`
- tests: `EnableNoninteractiveCrt()` then those wrappers
- both wrappers only called `EnsureObjectRegistration()`

After:

- one process-startup call: `EnsureObjectRegistration()` (enables CRT, then pulls library registrars)
- `EnsureMainWindowRegistration` / `EnsureWin32MainWindowPresenterRegistration` deleted
- model thread and `WinApp::Run` do not register
- `APPTRAVERSE_REGISTER(Application/MainWindow/MainWindowPresenter)` moved into `main_window_lifecycle.cpp` so both distill and load-only executables link them without a second Ensure* (that TU is a direct source of every consumer)
- `Win32MainWindowPresenter` registrar stays in `win_presenters.cpp` (direct exe/smoke source)
- `lifecycle.h` `#undef RegisterClass` after `windows.h` so `ae::Registry::RegisterClass` is not rewritten to `RegisterClassA`

## Helpers removed / inlined

- deleted the two Ensure* wrappers (decls + defs)
- inlined smoke `PreparePersistedState` into the load-only child-process test
- not inlined: `RequestStop` (WM_CLOSE after Main, WM_QUIT after Main, headless/smoke tests)
- not inlined: `WaitPublished` / `TestDir` / `LoadUiFromSession` / `PersistFixture` / `PersistedApplicationExists` (reused in tests)
- not inlined: `RegisterWindowClasses`, `PaintLoading`, `WndProc`, `OnPublished`

## ModelSession (kept)

Still the shared Win32 + headless model-thread path:

- `state_dir`, `PublicationChannel`, `mu`/`cv`, `stop`, `RequestStop`, `Run`
- `notify_hwnd` / `done_event` (WinApp vs headless that never sets them)

No stage, getters, or test instrumentation added.

## WM_QUIT

While `loading_ != nullptr`, `WM_QUIT` is dropped (startup is not cancelable). After `OnPublished` sets `loading_ = nullptr`, `WM_QUIT` may `RequestStop()`. `WM_CLOSE` on Loading is still ignored. No deferred-quit queue.

## Distill / load-only

Unchanged architecture. Distill bootstrap remains `#ifdef APPTRAVERSE_ENABLE_DISTILLATION`. Load-only lifecycle has no missing-state branch, no `BuildMainWindowGraph` / `FinalizeDistilledGraph` / `SaveDistilledRoot`. Missing Application is fatal (`LoadApplication` assert).

## Remaining exceptions (not this slice)

- `LoadStoredAncestorLayers` / `LoadStoredAncestorLayersFromRoot` after LoadRoot — TODO in `plan.md` (aether-objects)
- `if (notify hwnd)` / `if (done_event)` — real WinApp vs headless branch
- WndProc `app == nullptr` before `WM_NCCREATE`
- `loading_ == nullptr` as the “Main exists” flag for close/quit
- Cursor `user-apptraverse` MCP schema still has no `source_dir` (**BLOCKED**). Local runner is not attached MCP success.

## Tests / artifacts

Worktree runner, incremental, no clean/rebuild:

| target | run_id / artifact | status |
| --- | --- | --- |
| demos + headless + Win32 smoke | `apptraverse-build/20260908-220020-c095cc` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_win32_smoke_test`) |

## Commits / push

- `cedf9fa7e193e3fcc5a01d71f73e9e68c31a3aab` — Simplify main-window startup and registration

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed.

# Coding-agent rules — progress


## Identity

- Branch: `prep/deps-objects-assert-mcp-v1`
- Status: implemented, verified. Not accepted.

## Where the rules live

Existing format: `.cursor/rules/*.mdc` (same as `apptraverse-headless-chat-tests.mdc`).
No `AGENTS.md` in this repo; none was added.

Added always-apply:

- `.cursor/rules/apptraverse-coding-agent.mdc`

Left unchanged: `.cursor/rules/apptraverse-headless-chat-tests.mdc`.

No application rebuild (instructions/docs only). No clean/rebuild.

## Current skeleton review (next iterations; not fixed here)

Not treated as violations:

- `EnsureMainWindowRegistration` + `EnsureWin32MainWindowPresenterRegistration` in `main()` — one startup point, two TUs
- `if (notify hwnd)` / `if (done_event)` — WinApp vs headless
- `if (app == nullptr)` in WndProc before `WM_NCCREATE`
- `RegisterWindowClasses` / `PaintLoading` — allowed Win32 groups / WndProc
- Distill bootstrap is behind `APPTRAVERSE_ENABLE_DISTILLATION`
- Close-during-Loading tests/API are gone; Loading has no system Close

Recorded for later:

- `WM_QUIT` in the GUI loop still calls `RequestStop()` even before Main exists, so startup is not fully non-cancelable
- Tests (and smoke) still call `EnableNoninteractiveCrt()` and then `Ensure*`, which also enables CRT via `EnsureObjectRegistration`
- Ancestor-layer reload remains an App Traverse pass after LoadRoot (already TODO in `plan.md`)
- Cursor `user-apptraverse` MCP still has no `source_dir` (**BLOCKED**)

## Commits / push

- `dcc14902fb6828582d1d7d123faa30ac45fd6c64` — Add App Traverse coding-agent rules

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed.

# Main-window startup simplify and distill/load-only split — progress

## Identity

- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Status: implemented, verified. Not accepted.

## Removed

- `ModelStartupStage`, `EnterStage`, `hold_stage`, `SetHoldStage`, `--hold-stage`
- Cancelable startup and close-during-Loading (tests and production)
- Test instrumentation on `ModelSession`: `published`, `finished`,
  `distilled_this_run`, `stage`, thread ids, model object addresses/ids/geometry
- `ApplicationStateExists` helper; `accept_input_`; `stop_requested_`;
  `GuiPresenterClassId`; `DestroyGuiMirror` wrapper
- Duplicate registration / `EnableNoninteractiveCrt` inside `WinApp::Run` and
  the model thread

## ModelSession (kept)

Shared production model-thread path for Win32 and headless tests:

- `state_dir`, `PublicationChannel`, `mu`/`cv`, `stop`, `RequestStop`, `Run`
- `notify_hwnd` / `done_event` (WinApp vs headless that never sets them)

## Dev / load-only

- Compile definition: `APPTRAVERSE_ENABLE_DISTILLATION`
- `win32_main_window_runtime_demo` — distill-enabled; bootstrap if state missing
- `win32_main_window_runtime_demo_load_only` — no definition; load only; missing
  Application is fatal
- Lifecycle `.cpp` is compiled per target (not one STATIC lib), so the ifdef is
  real. Load-only lifecycle obj has `LoadApplication` and no
  `FinalizeDistilledGraph` / `SaveDistilledRoot` / `BuildMainWindowGraph`.

Registration: `main()` registers model + Win32 presenter once, then `WinApp::Run`.

## Tests / artifacts

Cursor `user-apptraverse` still has no `source_dir`. **BLOCKED**.
Worktree runner (incremental, cmake regenerated ninja; no clean/rebuild):

| target | run_id / artifact | status |
| --- | --- | --- |
| simplify startup | `apptraverse-build/20260908-213039-bcd573` | ok |
| distill + load-only + smokes | `apptraverse-build/20260908-213326-84d24d` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_win32_smoke_test`) |

Missing-state fatal is the load-only child-process smoke (non-zero, no Main, no Application object).

## Commits / push

- `628174df8a5b006b9d783a331751a829034e671f` — Simplify main-window startup and remove test state machine
- `1e91b0e15c484ed4fa2584d694dcfc26a06ea415` — Split development distillation and load-only targets

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed.

# Main-window lifecycle defensive-check cleanup — progress

## Identity

- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Status: implemented, verified. Not accepted.

## Removed checks (unreachable or already guaranteed)

- `Win32MainWindowPresenter::OnLoad`: no `hwnd != nullptr` early return, no
  `!window` check, no `assert(hwnd == nullptr)` / `assert(window)`, no
  bool-return recovery if `CreateWindowExW` fails.
- `DestroyNative` / destructor `DestroyWindow`: gone. Native teardown is
  `OnUnload` only, called from `UnloadPresenters` after successful GUI init.
  Model-side Win32 objects never run that pass, so the destructor does not
  guess whether an HWND exists.
- `presentation_initialized` and per-presenter "already initialized" / second
  `OnLoad` protection.
- `InitializePresenters` bool return and "OnLoad failed" recovery.
- `OnPublished`: empty-bytes return, stacked graph asserts, init-failure
  abort-to-Loading, `assert(loading_)` before destroying Loading.
- Startup recovery (`AbandonStartup`, `exit_code_`, `joinable()`, repeated
  `assert(notify_)` / `assert(done_event)` at shutdown).
- WndProc `assert(msg == WM_GETMINMAXINFO)` when `app == nullptr`.
- ModelSession duplicate `assert(buffer)` / `assert(presenter)` after the
  stage that already produced them.
- Tests: idempotent second `InitializePresenters`; `TestPresenterInitFailure`
  (recoverable OnLoad failure is not a supported state).

## Kept `if`s (real state-machine branches)

- `InitializePresenters` / `UnloadPresenters`: `dynamic_cast` skip of
  non-Presenter objects on the reachable walk.
- `OnPublished`: `stop_requested_` — publication can arrive after close during
  Loading; skip presentation init.
- `RequestStop`: already requested (Loading and Main can both send WM_CLOSE).
- `loading_ != nullptr` at shutdown: close during Loading vs after Main replaced
  it. Success path destroys Loading then sets `loading_ = nullptr` because
  that nullable handle is the two-stage machine.
- `ui_application_` before `UnloadPresenters`: graph exists only after a
  completed presentation pass; close during Loading never loaded it.
- WndProc: `WM_NCCREATE` binds userdata; `app == nullptr` is legal before that
  (`WM_GETMINMAXINFO`); after bind, dispatch to `Handle`.
- Message loop: `WAIT_OBJECT_0` vs queued input; `WM_QUIT`.
- ModelSession: `EnterStage` / `stop`; first launch vs existing state;
  `done_event` / notify HWND (WinApp vs headless tests that never set them).

## Fatal (unrecoverable) conditions

- `CreateEventW` for `done_event` returns null.
- `CreateWindowExW` for notify or Loading returns null.
- `CreateWindowExW` for Main returns null (`OnLoad`).
- `std::thread` construction throws (uncaught; never enters the loop).

No recovery, no keep-Loading, no presenter-without-HWND, no bool `OnLoad`.

## Tests

Headless: startup, existing-state, mirror identity, most-derived Test
presenter, `InitializePresenters` once then `UnloadPresenters` once,
model-side OnLoad/OnUnload not called, object destructor does not `OnUnload`,
thread ownership, stop during Loading, stop after Ready.

Win32 smoke: Loading then Main; GUI class `Win32MainWindowPresenter`; close
during Loading does not create Main; GUI teardown destroys Main HWND.

## MCP / artifacts

Cursor `user-apptraverse` still has no `source_dir`. **BLOCKED**.
Worktree runner (incremental, no clean/rebuild):

| target | run_id / artifact | status |
| --- | --- | --- |
| `apptraverse_main_window_headless_check` + `apptraverse_main_window_win32_smoke_check` | `apptraverse-build/20260908-211920-2b7853` | ok (`publication_channel_test OK`, `main_window_lifecycle_test OK`, `main_window_win32_smoke_test OK`) |

Did not rebuild `apptraverse_event_sourced_core_test`: it pulls the full Aether
client (sodium) and is outside this slice. `presenter.h` there is only a
class-id check.

## Commits / push

- `c0008de19a234eda79b63bb7c08e316f5bcf763b` — Remove defensive state checks from main-window lifecycle

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed.

# Object-graph presenter — progress

## Identity

- Base SHA: `9bae06bd881e043b4f67ded7a3d731e68726dfa5`
- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Status: implemented, verified. Not accepted.

## App Traverse library changes

- `LoadInitialPublication` injects serialized layers then calls
  `DomainGraph::LoadRoot` so aether-objects picks the most-derived registered
  factory. No App Traverse preferred-class map.
- Distilled `Node::base` object layers are omitted from UI publication buffers.
- After LoadRoot, `LoadStoredAncestorLayers` loads stored ancestor class
  layers onto the already-constructed most-derived object (needed when
  persisted data has only `MainWindowPresenter` and the registry created
  `TestMainWindowPresenter` / `Win32MainWindowPresenter`).
- `InitializePresenters` walks reachable live objects from the GUI root and
  calls `Presenter::OnLoad()` once. Object Load does not call it.
- `Presenter` documents OnLoad/OnUnload as GUI presentation hooks; runtime-only
  `presentation_host` is not serialized. `presentation_initialized` was removed
  in the defensive-check cleanup (init runs once per GUI mirror).

## Example changes

- Graph: `Application` → `MainWindow` (Node, schema v3: x,y,width,height,presenter)
  → `MainWindowPresenter` (not Node) → `Win32MainWindowPresenter` (HWND runtime-only).
- Cycle: `presenter->window` is the same MainWindow in that Domain.
- `WinApp` no longer owns a presenter member or calls `Create`. It loads the
  GUI graph, runs `InitializePresenters`, then destroys Loading. No
  `dynamic_cast` to Win32. If `RequestStop` already happened, skip init.
- Model-side most-derived Win32 presenter exists after Load and does not create HWND.

## Tests

Headless `apptraverse_main_window_lifecycle_test` (no Win32):

- A. Neutral `MainWindowPresenter` in the buffer + registered
  `TestMainWindowPresenter` → LoadRoot materializes Test
- B. OnLoad not called after model load / serialize / GUI deserialize
- C. `InitializePresenters` once; `presenter.window` resolved; back-pointer
- D. Model vs GUI: same ObjIds, different addresses/Domains
- E. Cycle walk terminates; dropping root+keepalive does not leak the GUI graph
- F. Model-side Test exists after LoadApplication; OnLoad not called there

Windows smoke (existing checks plus):

- GUI presenter class is `Win32MainWindowPresenter`
- exactly one Main window
- close during Loading does not create Main

## MCP jobs / artifacts

Cursor `user-apptraverse` schema still has no `source_dir` argument.
**BLOCKED** for that attached MCP process. Local incremental runner is not
claimed as MCP success.

Worktree runner `tools/runners/run_apptraverse_build.py` (MSVC env, stage=build,
no clean/rebuild):

| target | run_id / artifact | status |
| --- | --- | --- |
| `apptraverse_main_window_lifecycle_test` | `apptraverse-build/20260908-203315-e7a483` | ok (test exe OK) |
| `apptraverse_main_window_headless_check` + `apptraverse_main_window_win32_smoke_check` | `apptraverse-build/20260908-203339-eb1692` | ok |

## Known limitations

- Cursor user-level MCP is still the original checkout binary (no `source_dir`).
- Ancestor-layer reload is an App Traverse pass after LoadRoot; aether-objects
  `Load(Derived)` does not itself load ancestor class layers when the derived
  class has no stored version.
- Presenter local state (DPI, monitor, scroll) is not implemented.
- `main` was not changed.

## Commits / push

1. `86f5d38019c88a5397cb8dc19d3efcf8971bbca4` — Use aether-object descendant resolution for GUI mirror loading
2. `63a6eff6080d883819855ce28c9459af065c8e52` — Move main window presentation into object graph

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed. History not rewritten.

## git status --short

Untracked caches only (not committed):

```
?? tools/mcp/__pycache__/
?? tools/runners/__pycache__/
?? tools/runtime/__pycache__/
```

# MCP worktree-aware — progress

## Problem

User-level MCP (`user-apptraverse`) launches `tools/mcp/apptraverse_mcp.py` from the
checkout that contains that file. `repo_root()` was `Path(__file__).resolve().parents[2]`.
Build/test tools therefore always used that original tree, even when Cursor was
opened on another git worktree.

Observed against this worktree, from the still-running original MCP process:

- job `20260908-195143-ed5b76` / artifact `apptraverse-jobs/20260908-195143-ed5b76`
- nested `apptraverse-build/20260908-195145-6ee81a`
- `ninja: error: unknown target 'apptraverse_main_window_headless_check'`
- response had no `source_dir` (stale server schema)

Cursor MCP config was not rewritten. No per-worktree MCP server was registered.

## API

Optional `source_dir` on START (and on excerpt/log query, which read artifacts).
STATUS/CANCEL/STOP take `job_id` / `process_id` only; the server remembers the
canonical checkout in `.artifacts/mcp-source-index/` under the MCP server tree
and in `job.json` / `request.json` / `process.json` of the selected checkout.

Omitted `source_dir` keeps the previous default: the checkout that launched the
server. Never cwd.

Invalid / missing / non-App-Traverse paths return `failure_kind=invalid_source_dir`
and `state=failed` (no Python exception). Relative paths are rejected.

Tools with `source_dir`:

- `apptraverse_build_start` / `status` / `cancel` / `failure_excerpt`
- `apptraverse_platform_start` / `status` / `cancel` / `failure_excerpt`
- `apptraverse_process_start` / `status` / `stop`
- `apptraverse_chat_headless_test_start`
- `apptraverse_chat_p2p_headless_test_start`
- `apptraverse_runtime_log_query`

## Commit SHA

`5699c613e55f66ccea6eece1f2009f28d8dd6f26` — Make App Traverse MCP worktree-aware

## Unit tests

`python -m unittest tools.mcp.test_apptraverse_mcp tools.runners.test_run_apptraverse_job tools.runners.test_run_apptraverse_platform_job`

- omitted `source_dir` → `start_job(repo_root(), …)`
- explicit fixture `source_dir` → runner receives that path, not MCP `repo_root()`
- two roots: status/excerpt do not mix jobs or same-run-id artifacts
- missing path / non-App-Traverse dir / relative path → `invalid_source_dir`
- existing MCP tests remain green

## Real MCP jobs against this worktree

`source_dir=C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
profile `win64-ninja-msvc-debug`

The attached Cursor `user-apptraverse` process is still the original checkout
binary and ignores `source_dir`. Proof of the new server used this worktree's
`apptraverse_mcp.py` over MCP stdio (cwd `C:\Temp`) and the same tool functions.

| target | job ID | artifact ID | nested build artifact | status |
| --- | --- | --- | --- | --- |
| `apptraverse_main_window_headless_check` | `20260908-195713-0fef9b` | `apptraverse-jobs/20260908-195713-0fef9b` | `apptraverse-build/20260908-195714-45078a` | ok |
| `apptraverse_main_window_lifecycle_test` | `20260908-195737-53326d` | `apptraverse-jobs/20260908-195737-53326d` | `apptraverse-build/20260908-195739-d8ba7b` | ok |
| `apptraverse_main_window_win32_smoke_check` | `20260908-195739-1c8eb3` | `apptraverse-jobs/20260908-195739-1c8eb3` | `apptraverse-build/20260908-195741-ce8814` | ok |
| stdio preflight (cwd Temp) | `20260908-195928-053c6d` | `apptraverse-jobs/20260908-195928-053c6d` | — | ok |

Canonical `source_dir` in job metadata and public payloads:
`C:\Users\nickc\Projects\apptraverse-prep-deps-assert`

The headless-check target exists only in this worktree; the stale original MCP
job failed with unknown target. After `source_dir`, ninja found the target here.

Assert/crash path was not regressed: these jobs completed `ok` through the
existing noninteractive worker (no assert dialog).

# Main-window skeleton — progress

## Identity

- Base SHA: `24daa80dfb39edfcc1261cd15df2d62d1ce32fa9`
- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`

## Commits

1. `e9bca9adfeba9b0c111486276646b1265a97452e` — Fix PublicationChannel TakePublished so a consumer buffer cannot be cleared.
2. `6a58e2074a108ca85599b6cd191ef9982f8011ea` — Add serialized initial publication without GUI-thread model graph copies.
3. `0f2540bc1d55e8853a0851376ba31e34fb7c9c20` — Add Loading/main-window example with a dedicated model thread.
4. `57f4fc30db7b75354d82085e81924f3ef3b520fc` — Add headless lifecycle and Win32 smoke tests for the main window.
5. `1a078f9f9a6b1f907749d1a8d5f6fbca7da3c0da` — Record the main-window skeleton plan and progress.
6. `75a1af396b86237bf6c6b770a8db50e4ef2a159f` — Name the documentation commit in Progress.md.
7. `4135ca5afa46b7f731afa3ba295ff1baf63aab27` — Remove premature DPI state from main window skeleton.

## DPI cleanup

DPI was removed from the new skeleton because `plan.md` defers DPI/screen system events to a later stage. This slice only needs Loading → model thread → serialized mirror → Main → shutdown.

Removed from the example and its tests:

- `MainWindow::dpi`
- reflection / Load / Save of DPI
- `kDefaultDpi`
- `model_window_dpi`
- DPI assertions

`MainWindow` now has only `x`, `y`, `width`, `height`. Schema version is 2; v0 and v1 Load throw (no conversion). Tests use clean temp state directories. `plan.md` later-stage DPI/screen architecture is unchanged.

## App Traverse library changes

- `PublicationChannel::TakePublished` now CAS-marks `in_ui_` before clearing `published_`, so `AcquireProducer` cannot wipe the buffer the consumer is reading. Added `TakePublishedCopy()`.
- `SerializeInitialPublication` / `LoadInitialPublication`: root ObjId + existing graph-fragment bytes. GUI Domain creates shells from the buffer; it does not read model objects.
- `EnableNoninteractiveCrt()` hooked from `EnsureObjectRegistration()` so Debug asserts abort to stderr instead of a modal dialog.
- `apptraverse` PUBLIC-links `aether::objects` + `aether::miscpp` only. Full `aether` is linked by chat/presence/model-ui targets that include `aether/clock.h` / `aether/all.h`. This slice does not compile the network client.

## Example-only changes

`examples/main_window_runtime_demo/`:

- `Application` → `MainWindow` (Node) with `x, y, width, height`
- `ModelSession::Run` on the model thread
- Win32 `WinApp`: Loading HWND, notify HWND, presenter, empty Main HWND

## Model lifecycle

Fresh state (model thread only):

1. create storage + Domain + graph
2. distill (`FinalizeDistilledGraph` + `SaveDistilledRoot`)
3. drop Application + Domain + storage
4. new storage + Domain in the same thread
5. `LoadApplication`
6. serialize into `PublicationChannel<3>` and notify GUI

Existing state: skip 1–3; load and publish.

Stop (`RequestStop` + `cv.notify_all`) is checked at each stage, including mid-serialize before `PublishProducer`. Cleanup always runs on the model thread: Application, Domain, storage, then `SetEvent(done_event)`.

## Initial publication format

`uint32 root_id` followed by `SerializeObjectGraphToBuffer` (layer count, per-layer obj/class/version/bytes, then Node generation table). GUI `LoadInitialPublication` injects layers into RAM storage, creates shells using a registered factory (skips `ae::Obj` base layers), loads, and `FinalizeUiNodeState` (clears `base` and `journal`).

## Domain ownership

- Model Domain + `DirectoryDomainStorage`: model thread only. Never passed to GUI.
- GUI Domain + `RamDomainStorage`: GUI thread (or the test consumer thread) after `TakePublishedCopy`.
- Same ObjIds, different C++ addresses, different Domains. Observation atomics store integer addresses for tests; GUI must not dereference them.

## MCP / artifacts

User-level `user-apptraverse` MCP is bound to the original App Traverse checkout, not this worktree.

- MCP job `20260908-193409-2f3650` / artifact `apptraverse-jobs/20260908-193409-2f3650` → failed: `unknown target 'apptraverse_main_window_headless_check'`
- Nested MCP build artifact `apptraverse-build/20260908-193411-af0768`

Local runner in this worktree (authoritative for this slice):

- DPI-cleanup build `apptraverse-build/20260908-193412-68f37e` status=ok

## Headless tests

Profile `win64-ninja-msvc-debug`.

- `apptraverse_main_window_lifecycle_test` — OK after DPI removal
  - fresh: distill + destroy Domain + reload + initial publication
  - existing: no second distill
  - initial mirror: same ObjIds, different addresses/Domains, same MainWindow bounds, UI `base` invalid and journal empty
  - thread ownership: create/destroy on model thread; GUI copy on consumer thread
  - stop while Distilling: join completes, `published` stays false
  - stop after Ready: join completes
- Synchronization is `condition_variable` waits, not `sleep()` as proof.

## Windows smoke

`apptraverse_main_window_win32_smoke_test` — OK after DPI removal (`main_window_win32_smoke_test OK`, exit 0)

- in-process: Loading appears, Main appears, Loading gone, close Main, thread exits
- in-process close during Loading (`hold_stage=Distilling`): join, no Main
- child `win32_main_window_runtime_demo`: same Loading → Main → close → process exit 0
- child close during Loading (`--hold-stage 2`): process exit 0

Search after cleanup: `dpi`, `DPI`, `WM_DPICHANGED`, `GetDpiForWindow` are absent from `examples/main_window_runtime_demo` and `tests/main_window_*.cpp`.

## Shutdown

- During Loading: GUI `WM_CLOSE` → `RequestStop` + wake; message loop until `done_event`; join; destroy Loading/notify/GUI Domain on GUI thread.
- After Ready: same stop path; presenter/Main HWND destroyed on GUI thread after join.
- No `TerminateThread`. Model objects are not freed on the GUI thread.

## git status --short

After recording the implementation SHA (documentation-only follow-up):

```
 M Progress.md
?? tools/mcp/__pycache__/
?? tools/runners/__pycache__/
?? tools/runtime/__pycache__/
```

# Main-window skeleton — presenter WndProc, wakeup, fail-fast

Status: implemented, verified locally. Not accepted.

## Identity

- Base SHA: `644729c1f69c97055afd90ae88fcda336b8d692e`
- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree / source: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- `CMAKE_HOME_DIRECTORY`: `C:/Users/nickc/Projects/apptraverse-prep-deps-assert`
- Build dir: `build/win64-ninja-msvc-debug`
- Profile: `win64-ninja-msvc-debug` (incremental; no clean)

## Commits

1. `af70e9c` — Native Main `RegisterClassW` / `WndProc` owned by `Win32MainWindowPresenter`. `WinApp` owns Loading/notify only. `lpParam` is the presenter. `WM_CLOSE` posts `WM_APPTRAVERSE_STOP` to the notify HWND.
2. `158682f` — Lost wakeup: `stop` is a plain `bool` under `mu`; `RequestStop` sets it then `notify_all`. Publication `NotePublished`/`PublishProducer` under the same mutex waiters use. Model graph/Domain/storage leave an inner scope before `SetEvent(done_event)`.
3. (this file) Fail-fast: `LoadApplication` uses `WriteFatalStderr` + `abort`, not `assert`. `fflush`/`FlushFileBuffers` before abort. Regression tests for missing Application. Coding-agent rules: Æther vs `WNDCLASS`, CV mutex, `done` after model scope, GUI `STD_ERROR_HANDLE`.

## Where Main lives

- `Win32MainWindowPresenter::OnLoad`: `RegisterClassW` then `CreateWindowExW(..., this)`
- `Win32MainWindowPresenter::WndProc` in `win_presenters.cpp`
- `OnUnload`: `DestroyWindow` then `UnregisterClassW`

## Fail-fast notes

- Missing Application: `if (!root) { WriteFatalStderr(...); abort(); }`
- Do not compile registrar TUs with `NDEBUG` against Debug aether-objects (`Factory` layout is `#ifndef NDEBUG`). The fatal-ndebug *test* proves abort+diagnostic; it is a Debug binary.
- GUI-subsystem load-only: diagnostic goes to `GetStdHandle(STD_ERROR_HANDLE)` as well as CRT stderr.

## Compile (this tree)

`win_app.cpp` / `win_presenters.cpp` (demo target):

```
cl.exe /TP -DAE_DISTILLATION=1 -DAE_FILTRATION=1 -DAPPTRAVERSE_ENABLE_DISTILLATION -DNOMINMAX -DWIN32_LEAN_AND_MEAN ... /std:c++20 -MDd /utf-8 /Zc:preprocessor -c .../windows/win_app.cpp
cl.exe /TP ... -c .../windows/win_presenters.cpp
```

Includes resolve to `C:\Users\nickc\Projects\apptraverse-prep-deps-assert\include` and this worktree's `_deps`.

## Tests actually run (this checkout)

| target | artifact | status |
| --- | --- | --- |
| `apptraverse_main_window_headless_check` | `apptraverse-build/20260909-003112-345383` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_fatal_ndebug_test`) |
| `apptraverse_main_window_win32_smoke_check` | `apptraverse-build/20260909-003159-203e12` | ok (`main_window_win32_smoke_test OK`) |
| `win32_main_window_runtime_demo` + `_load_only` | `apptraverse-build/20260909-003136-993f0c` | ok (up_to_date; linked in `20260909-002902-203659`; exercised as smoke children) |

Exes:

- `build/win64-ninja-msvc-debug/tests/apptraverse_main_window_fatal_ndebug_test.exe`
- `build/win64-ninja-msvc-debug/tests/apptraverse_main_window_win32_smoke_test.exe`
- `build/win64-ninja-msvc-debug/examples/main_window_runtime_demo/windows/win32_main_window_runtime_demo.exe`
- `build/win64-ninja-msvc-debug/examples/main_window_runtime_demo/windows/win32_main_window_runtime_demo_load_only.exe`

Cursor `user-apptraverse` MCP still has no `source_dir` (**BLOCKED**). Local runner only.

## Limitations / TODO

- `LoadStoredAncestorLayers` still in App Traverse (`plan.md`)
- Do not restore close-during-Loading
- Not accepted-by-user

