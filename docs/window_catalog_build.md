# Window-only development build

The active CMake graph builds `win32_window_catalog_demo`, its load-only binary,
and their UI/model tests. Historical chat, SharedNode and old Surface demos are
not build targets. The Aether network client is not fetched or linked.

All AppTraverse-owned serialized classes have one current schema, v0, retaining
ClassId. The previous development formats are deliberately unsupported. Use a
new `--state-dir` for this revision; existing profiles are never deleted or
migrated automatically.

Serialized scalar defaults are set by explicit `ae::ObjProp` creation
constructors (distillation/dynamic creation), not in member declarations.
Deserialization supplies persisted fields. Native handles, replay cursors and
other nonserialized lifecycle state are initialized by both construction paths.
`base_{*this}` in the object macro is an inheritance binding, not a data default.
Business defaults in non-object records (for example journal retention) remain.

C++ exceptions and RTTI are disabled. Unsupported version readers and throwing
stubs were removed. Fatal boundary diagnostics remain stderr+abort, not a logging
subsystem. Publication profiling and instrumented storage logging are removed.
The dependency telemetry is disabled by `cmake/aether_user_config.h`.

MinSizeRel intentionally retains `/GS-`, `/GUARD:NO`, `/fp:fast`, and the size/LTCG
settings requested for this development demo. Debug/Release test code must use
the same schema and object layouts as the linked libraries.

Optional extended controls dispatch through `Presenter::OnCommand` on the
actual child object. Registration no longer installs a process-global router.
