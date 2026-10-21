# Application / state evolution matrix

This suite runs independent executables against real `DirectoryDomainStorage`
directories. The Python runner owns the expected values; each operation starts a
new process. Working AppTraverse classes remain at schema v0.

## Executables and schemas

| Executables | EXE revisions | `LayerBase` schema | `LayerDerived` schema |
| --- | --- | --- | --- |
| `apptraverse_evolution_stable_a/b/c` | A / B / C | 0 / 0 / 0 | 0 / 0 / 0 |
| `apptraverse_evolution_schema_a/b/c` | A / B / C | 0 / 1 / 1 | 0 / 0 / 1 |

EXE A, B and C increment `value` by 1, 10 and 100 respectively. State revisions
are successive saved values, not global schema numbers. `StateRoot` always has
schema 0; base and derived class layers evolve independently.

The graph has root ID 91001, child ID 91002, two aliases of that child and a
child-to-root cycle. Initial data is `value=11`, `child_value=37`, `label=initial`.
Updates set `label=updated-A/B/C`. New readers supply `added_base=999` or
`added_derived=777` when reading the corresponding old schema. Creation/update
with those fields supported saves nondefault values `1000+value` and
`2000+value`, so subsequent reloads must preserve data rather than reapply defaults.

When both old and new schema slots exist, the native loader invokes both `Load`
overloads. The fixture keeps an owning copy of the previous `backref` while
loading the newer layer so the cycle's target remains alive during replacement.
This lifetime rule belongs to the fixture loader.

## Build and run

The suite is enabled by `APPTRAVERSE_BUILD_EVOLUTION_TESTS=ON` under
`BUILD_TESTING` and requires Python 3.9 or newer. It is independent of
`APPTRAVERSE_BUILD_EXAMPLES`. Set the evolution option to `OFF` explicitly when
building without this test dependency.

Use an existing configured build directory; these commands build incrementally:

```sh
cmake --build <build-dir> --target apptraverse_application_evolution_fixtures
ctest --test-dir <build-dir> -R '^apptraverse_application_evolution_matrix$' --output-on-failure
```

For a multi-configuration generator, select the same configuration with
`--config Debug` for the build and `-C Debug` for CTest.

The runner also accepts the six executable paths directly, in A/B/C order:

```sh
python3 tests/application_evolution_matrix.py \
  --stable <build-dir>/tests/apptraverse_evolution_stable_a <build-dir>/tests/apptraverse_evolution_stable_b <build-dir>/tests/apptraverse_evolution_stable_c \
  --evolving <build-dir>/tests/apptraverse_evolution_schema_a <build-dir>/tests/apptraverse_evolution_schema_b <build-dir>/tests/apptraverse_evolution_schema_c
```

Use the actual executable paths, including configuration directories and `.exe`
on Windows. Paths containing spaces must be quoted. `--work-root <existing-dir>`
selects the parent of a **new** run directory; it never selects an existing state
profile. `--timeout <seconds>` limits each child process (default: 30 seconds).

Append `--sequence A,C,B,A` to replace the default chains with a chosen order.
The option is repeatable and permits repeated revisions, for example
`--sequence C,B,A,C,C`. The pair matrix and other checks still run.

## Default guarded matrix

There are 24 scenarios with the default chains:

- Two data updates using the same EXE A, with schema 0 throughout.
- All nine writer/reader pairs for each executable family.
- `A -> C -> B -> A` and `A -> B -> C -> B -> A -> C` for each family.
- A future schema-2 slot injected into C's derived layer, including attempts to
  inspect and update it.

Before an update, a separate `inspect` checks loaded values and migration
defaults. After saving, another process reloads and checks the result. Read-only
EXE changes must leave storage unchanged. Following an incompatible transition,
the last compatible writer reopens and verifies the last successful state;
later transitions continue from that state.

The fixture rejects unsupported stored schemas **before constructing storage or
a Domain**. It returns exit code 2 with `INCOMPATIBLE`, object/class identity and
the unsupported/supported schema numbers. This admission check belongs only to
`application_evolution_fixture.cpp`; it does not change or prove the safety of
the generic AppTraverse/Æther loader.

Expected incompatibility is a passing scenario only when both the refusal and
storage preservation checks succeed. An unexpected exit, acceptance, value
mismatch, timeout or storage change is a failure. In the future-schema scenario,
the runner first proves the rejected file set unchanged, then removes only the
file it injected and verifies that the original state remains readable.

## Native diagnostic: do not convert failure into success

Append `--native` to the direct runner command. It bypasses the fixture's
admission check and runs `A -> B -> A` and `B -> C -> B` with the evolving
executables. A supplied `--sequence` replaces these diagnostic chains.

Saving a new version can leave an older version slot on disk. A native old
reader can then select stale bytes despite returning success. The two default
diagnostics are designed to expose this independently for base `value` and
derived `label`. The expected symptoms of the current stale-slot behavior are
`value: expected 21, got 11` and `label: expected 'updated-C', got 'initial'`.

These are **FAIL results with runner exit code 1**, not expected failures counted
as PASS. Do not suppress the exit code or mark the diagnostic `WILL_FAIL` to
make a regression gate green. The ordinary guarded matrix and the native
diagnostic answer different questions. A native load error is also reported as
FAIL here; the diagnostic is not the guarded compatibility contract.

## Evidence and limits

Every run preserves its fresh directory and prints its path. `results.json`
contains the scenario outcomes and SHA-256 identities of the executables.
Per-process JSON logs contain command, exit code, stdout/stderr, and storage
manifests before and after process termination. Manifests cover relative paths,
directories, file sizes and SHA-256 bytes; inspection and rejected operations
must not change them. The runner also verifies stable IDs, ClassIds across EXEs,
alias identity and the cycle. An EXE modified during a run fails the suite.

This suite covers the isolated fixture graph and normal process completion. It
does not establish transactional checkpoint import, crash/power-loss recovery,
concurrent writers, preservation of arbitrary unknown classes, or GUI behavior.
Node journals, compaction, business replay and production model persistence
remain covered by the existing AppTraverse tests. A guarded PASS does not mean
generic native rollback is safe.
