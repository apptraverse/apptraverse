# Minimal AppTraverse application template

Independent Windows application-template example. It uses the
shared platform stack (`ApplicationModelRuntime`, `WinApplicationHost`,
`ApplicationUiSession`) with its own model, events and window composition.

## Build

Configure the repo with `APPTRAVERSE_BUILD_EXAMPLES=ON` (default on Windows
developer builds). Targets:

- `win32_minimal_app`
- `win32_minimal_app_load_only`

## Run (development)

```text
win32_minimal_app.exe [--state-dir <path>]
```

Default state directory: `minimal_app_v0_state` next to the executable.

Every ordinary Debug launch creates a fresh distilled graph and ignores prior
USER state. The same Debug executable prepares embedded supply through
`--distill-only <output-directory>`; no separate Windows exporter is needed.
MinSizeRel uses the Debug-prepared embedded supply and restores `user.bin` when
present. Build Debug supply before configuring/building MinSizeRel.

## Run (load-only)

```text
win32_minimal_app_load_only.exe --state-dir <profile>
```

Loads embedded BACKING plus USER when present. A missing `user.bin` is a valid
first launch. This explicit restore consumer also supports Debug cold-load
tests; it is distinct from ordinary Debug fresh-graph behavior.

## Where things live

| Concern | Location |
|--------|----------|
| Application model type | `common/minimal_application.h` |
| Initial object graph | `windows/minimal_app_graph.cpp` |
| Model thread / persistence | `windows/minimal_app_model_session.cpp` |
| Win32 entry + host wiring | `windows/minimal_app_main.cpp` |

USER uses ordinary `RamDomainStorage`. Auxiliary window Close saves its graph
to RAM without file operations. After the model session completes, the
application saves its persistent graph and writes one `user.bin` using the
shared binary storage functions. There is no background or destructor flush.

## Extending

Add model nodes/events under `common/`, extend the graph builder for bootstrap,
and register presenters through existing UI composition APIs. Do not copy
platform message loops or publication plumbing — keep using `WinApplicationHost`.
