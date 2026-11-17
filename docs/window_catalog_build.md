# Window-only development build

## Windows 7 baseline and constraint layout

Use the Visual Studio 2022 desktop v143 toolset (verified compiler 14.44).
Initialize its developer shell before configuring the Ninja Debug preset, or
use the VS2022 preset. VS2026/v145 STL introduces mandatory newer-Windows APIs;
configuration rejects it instead of silently breaking the Win7 baseline.
`WINVER`, `_WIN32_WINNT` and `_STL_WIN32_WINNT` are set to `0x0601` by the shared
compile policy. The GUI executable declares a Windows 7 subsystem and supports
optional per-monitor DPI awareness on newer Windows. An import-table audit does
not replace running the executable and its compatible CRT on an actual Win7 OS.

The [system-settings/accessibility plan](system-settings-accessibility-plan.md)
defines the shared, local layout engine and the remaining platform stages.
WindowCatalog's main-window context menu (right-click or the keyboard Menu key)
provides automatic, horizontal and vertical layout, plus reset to the current
supply definition. Changes use model Events and survive USER Save/Load; system
font, colors, measured sizes and window-local layout caches are never saved as
layout overrides. Use a new `--state-dir` for this S0 development schema and pair
USER with its matching supply; older experimental profiles are preserved.

The main-window menu also selects Auto/System dark/System light and toggles
Soften dark theme. These local application preferences are stored in
`%LOCALAPPDATA%/AppTraverse/WindowCatalog/appearance.ini`, shared by the current
Windows user's WindowCatalog profiles and separate from portable `user.bin`.
Auto follows OS colors; forced modes use native light colors or the backend's
dark semantic palette when the OS does not expose that palette. Soften changes
dark surfaces to graphite (#202020/#2B2B2B). High Contrast always uses system
colors regardless of either preference. DPI, OS fonts and measured rectangles
are not persisted in the INI.

The active CMake graph builds `win32_window_catalog_demo`, its load-only binary,
and their UI/model tests. **P1.1** (see [versioning/PLAN.md](versioning/PLAN.md))
will **remove** SharedNode/replication sources still linked into core, legacy chat
demos, and Æther network client fetch — full deletion, not optional OFF.
Historical multi-platform Surfaces ports remain in tree for **P4** reuse.

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
