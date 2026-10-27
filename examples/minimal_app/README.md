# Minimal AppTraverse application template

Starter Windows application with one empty native desktop window. It uses the
shared platform stack (`ApplicationModelRuntime`, `WinApplicationHost`,
`ApplicationUiSession`) and contains no demo controls or business logic.

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

On first run (empty/missing state), the dev binary bootstraps a distilled
object graph, then runs normally.

## Run (load-only)

```text
win32_minimal_app_load_only.exe --state-dir <existing-profile>
```

Requires an existing profile created by `win32_minimal_app`. No bootstrap.

## Where things live

| Concern | Location |
|--------|----------|
| Application model type | `common/minimal_application.h` |
| Initial object graph | `windows/minimal_app_graph.cpp` |
| Model thread / persistence | `common/minimal_app_lifecycle.cpp` |
| Win32 entry + host wiring | `windows/minimal_app_main.cpp` |

Persistent save uses `SavePresentedWindow`, application `Save()`, and
`DirectoryDomainStorage::BeginSaveSession` and object-layer save at the end of the model session.

## Extending

Add model nodes/events under `common/`, extend the graph builder for bootstrap,
and register presenters through existing UI composition APIs. Do not copy
platform message loops or publication plumbing — keep using `WinApplicationHost`.
