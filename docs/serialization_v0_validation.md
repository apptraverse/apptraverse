# Serialization v0 and application evolution: validation

Date: 2026-10-21. Branch: `codex/serialization-v0-evolution`.

Requested source baseline: `8a5a690d396ff09af8bf75f78487f663e6cd4eb7`.
Build prerequisites: `1c499242b8b352ba5f082a3e55539eaba14e833c`.
Working schema reset: `8b40de6272046152ee470d4cc09a721e9e7f9b54`.
Evolution matrix: `262192073571ae59310714352df4c89be2229f80`.

This report does not claim a completely passing repository test suite or native
GUI acceptance. The table below distinguishes passing checks, baseline failures,
the long-journal timeout and deliberate native compatibility diagnostics.

## Environment and isolation

- Linux x86-64, GCC 13.3, Debug, C++20, `-fno-rtti`.
- `BUILD_TESTING=ON`, `APPTRAVERSE_BUILD_EXAMPLES=ON`,
  `APPTRAVERSE_BUILD_AETHER_DEMOS=OFF`, `APPTRAVERSE_BUILD_LINUX_GUI=OFF`.
- aether-objects source pinned at
  `79283b667cabf689da1059d618fe2a22abf51db4`, with the repository's existing local
  persistence and storage-access patches. Only a malformed patch hunk count was
  corrected; dependency serialization behavior was not changed.
- Existing tests received new dedicated `TMPDIR`, `TMP` and `TEMP` directories.
  The cross-process runners create unique directories themselves. No existing
  user/test state profiles were deleted or migrated.
- The Windows presenter worktree, its WIP/stash/BMP files and build profiles were
  inaccessible from this Linux workspace and were not modified. No changes were
  made to `apptraverse-app`.

## Working schemas

All **165 working object declarations** use v0: 81 in library headers and 84 in
examples. All working explicit `Load`/`Save` overloads use v0. Names, ClassId
strings and base classes are unchanged. Six ordinary test models were also
normalized; intentionally versioned evolution fixtures remain isolated in tests.

The [full baseline inventory](serialization_schema_v0_inventory.md) records all
236 original library, example and test declarations. Field review and persistence
checks specifically cover bitmap pixels, list notification flags, desktop
Z-order, graph aliases/cycles and the current Node journal format.

## Existing tests

The following **19 selected CTests passed** after the portable storage-test fix
and completion of the relevant executable links:

| Area | Passing CTest names (prefix `apptraverse_`) |
| --- | --- |
| Core/journals | `event_sourced_core_test`, `journal_retention_test`, `closed_event_graph_test` |
| Persistence/lifecycle | `model_persistence_test`, `model_runtime_stop_test`, `directory_domain_storage_test` |
| Native layers | `native_class_layers_test`, `native_class_layers_evolution_test` |
| SharedNode | `shared_node_foundation_test`, `shared_node_initial_sync_test`, `shared_node_incremental_event_test` |
| Checkpoint | `checkpoint_load_cycle_persistence_test`, `checkpoint_load_rehydration_test` |
| UI graph | `presenter_graph_persistence_test`, `ui_bitmap_publication_test`, `ui_bitmap_structural_publication_test`, `ui_edit_box_headless_test`, `ui_control_materialization_test` |
| Example model | `chat_demo_model_test` |

An additional **37 Surfaces functional cases passed** with:

```sh
apptraverse_surfaces_model_test --skip-long-journal
```

This mode explicitly prints the names of two skipped long-journal cases. The
default executable/CTest still runs all 39 cases. The functional run includes
restart, geometry, desktop Z-order, locale, publication, GUI proxy routing,
shutdown and object identity. The new Z-order assertion restores an order
different from creation order and verifies pointer identity after a new Domain
loads the saved graph.

The changed `chat_ui_runtime_demo` registration translation unit also passed a
syntax-only compile using the same includes, defines and no-RTTI policy. This
does not establish full Aether chat runtime or native UI behavior.

### Results that are not PASS

| Check | Result | Evidence and scope |
| --- | --- | --- |
| Full Debug `surfaces_model_test` | TIMEOUT, 300 seconds | During `TestCollectLiveReachableStableWithLongJournal`; first measurements completed, then the repeated 10,000-event samples exceeded the run limit. No performance threshold was changed. |
| `main_window_lifecycle_test` | FAIL | `dtor_calls == 1` at line 171. Reproduces on the baseline source with the same dependencies. |
| `dynamic_objects_add_test` | SIGSEGV | Reproduces on the baseline source with the same dependencies. Current-state stack reaches `Domain::RemoveObject(nullptr)` during model reload. |
| Windows/GTK native UI | Not run | Headless Linux validation does not replace platform GUI smoke tests. |

For the two example failures, an independent clean worktree/build used
`1c499242` (the requested baseline plus only build prerequisites), with separate
fresh profiles. Both targets built and produced the same failure categories.
The main-window failure reached the identical assertion. This comparison does
not prove the repository is otherwise regression-free.

The main-window diagnostic measured eight total presenter destructions after UI
hydration, while the existing model-only shutdown case passed with one. The
absolute global counter includes temporary UI graphs. The dynamic crash's null
cleanup dereference is established by a backtrace; its complete underlying
hydration/lifetime cause is deferred. No production lifetime fix is hidden in
the schema reset.

## Independent EXE/state matrix

See [the runner instructions](../tests/application_evolution_matrix.md).

| Family | EXE A | EXE B | EXE C |
| --- | --- | --- | --- |
| Logic changes only | base 0 / derived 0 | base 0 / derived 0 | base 0 / derived 0 |
| Isolated schema evolution | base 0 / derived 0 | base 1 / derived 0 | base 1 / derived 1 |

The program revisions update data differently (`+1`, `+10`, `+100`). The runner
checks state independently before and after updates, using a new process for
each operation. It verifies values, migration defaults, ObjId/ClassId stability,
aliases, cycles, executable hashes, and file/directory manifests. Expected
incompatibility passes only when an explicit refusal leaves all state bytes
unchanged and the last compatible executable can reopen them.

| Run | Result | Processes |
| --- | --- | ---: |
| Default matrix: all pairs, data-only updates, `A,C,B,A` and `A,B,C,B,A,C`, future schema rejection | **24 PASS, 0 FAIL** | 179 |
| Same matrix with custom chains `C,A,C,B,B,A,C` and `B,A,B,C,A` | **24 PASS, 0 FAIL** | 193 |
| Native diagnostic, admission guard bypassed | **0 PASS, 2 FAIL** | 14 |

The default/custom matrix's admission check exists **only in the fixture**. It
does not add a rollback guard to working AppTraverse classes or prove that the
generic native loader safely rejects all incompatible states.

The native diagnostic returned exit code 1 with these actual failures:

```text
FAIL native-A-B-A: evolving A: value: expected 21, got 11
FAIL native-B-C-B: evolving B: label: expected 'updated-C', got 'initial'
```

New-schema saves retain older version slots, so an old reader can read stale
data successfully. These failures are not converted to PASS or registered as
`WILL_FAIL` CTests. The guarded fixture refuses these transitions before loading.

During fixture validation, reloading a cyclic `ObjPtr` from both retained schema
slots exposed a lifetime issue. The fixture's version-1 `Load` now retains an
owning copy of the prior back-reference while replacing it. The repeated matrix
then passed; both stored slots, all graph checks and the independent oracle were
kept. This is a fixture loader responsibility, not a dependency serializer fix.

## Practical limits

This reset establishes new development v0 schemas; old experimental v0 keys
cannot always be distinguished from them. Use fresh state directories.

Journals remain covered by the existing Node tests, not by the small A/B/C
fixture. Crash recovery during save and transactional checkpoint import are not
proved by directory-storage refusal tests. Full native GUI validation and the
documented baseline issues remain separate follow-up work.
