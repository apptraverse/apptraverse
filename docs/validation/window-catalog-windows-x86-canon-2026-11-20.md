# Windows WindowCatalog x86 size profile and Canon review

The review covers Windows WindowCatalog and its x86 MinSizeRel recipe on
`a88f8e45`. GPT-6 Astra ultra reviewed Canon §25 and GPT-6.1 Sol high
implemented the corrections. Three gaps affected this scope.

| Canon rules | Finding | Correction |
|---|---|---|
| 10, 29 | The binary inspector expected 16-digit MSVC map addresses and CTest registered the binary policy only on x64. | The inspector reads PE32 and PE32+ with the corresponding map address widths. Both Windows bitnesses register the policy. |
| 27, 29 | The measured x86 recipe depended on `_CL_` and `_LINK_` to override generated options. | `APPTRAVERSE_MINIMUM_WINDOWS_X86` emits the measured options directly for MSVC x86 MinSizeRel. |
| 10, 28, 29 | The binary policy had no assertion against static SharedNode and network SDK linkage in WindowCatalog. | The catalog check rejects named network libraries, objects and symbols and accepts the ordinary Aether Objects library. |

## Source review

| Rule | Disposition in scope |
|---|---|
| 1 | Composition supplies required presenter, layout and device relationships. |
| 2 | Required native creation, session loading and shutdown I/O use their existing failure boundaries. |
| 3 | Catalog composition authors persistent defaults and Event creation fills serialized arguments. |
| 4 | Lazy windows, absent displays, native readiness and publication retain their lifecycle conditions. |
| 5 | Native commands submit model proxy calls and the mirror publishes committed state. |
| 6 | Windows measures native controls and converts coordinates. The model owns display resolution and constraints. |
| 7 | The generic application template and Windows host remain independent of the catalog. |
| 8 | The Windows entry point invokes one catalog registration that selects the required schema. |
| 9 | Ordinary Debug creates a fresh graph and produces the supply. The explicit load-only path consumes it. |
| 10 | The embedded consumer links the static C++ runtime. The updated policy checks the map and imports in both bitnesses. |
| 11 | USER, LOCAL and BACKING use independent ordinary RAM stores. |
| 12 | Child Close preserves state in RAM. The shutdown hook persists USER and LOCAL. |
| 13 | Objects, Events and publication retain native serialization. |
| 14 | Session storage outlives its Domain and presenters own native resources. |
| 15 | Add clones the prefab through the temporary Domain and the canonical Clone path. |
| 16 | Supply metadata and the overlay preserve constant ownership and shared policy references. |
| 17 | Closed window identities and references remain lazy. |
| 18 | Immediate compaction preserves base, history and replay through the existing Node contracts. |
| 19 | Close unloads the presentation, Open reuses its identity and Delete removes membership and LOCAL records. |
| 20 | Native labels resolve the active locale through the published model. |
| 21 | Native observations enter model Events and the presenter applies the resolved monitor placement. |
| 22 | Headless and Windows consumers share the catalog model. Native and model checks exercise different boundaries. |
| 23 | Catalog state continues to use its existing S0 schema. |
| 24 | Required I/O and native actions execute outside assertions and handle failures at their boundaries. |
| 25 | The catalog policy excludes unused text controls and the separate catalog language button. |
| 26 | The x86 profile preserves C++20, static CRT, disabled compiler RTTI and exceptions, and runtime-only native resources. |
| 27 | The measured profile is opt-in for Windows x86 MinSizeRel. The default build flags retain their existing values. |
| 28 | WindowCatalog excludes SharedNode and the network SDK. The separate chat extension lies outside this review. |
| 29 | The executed checks below identify the architecture, build configuration and test boundary. |
| 30 | The fixes are prepared on a feature branch for integration under the standing main publication instruction. |

## Executed Windows checks

- MSVC v143 19.44 generated the expected x86 MinSizeRel command lines,
  including `/Ob2 /fp:fast /GS-` and `/LTCG /OPT:REF /OPT:ICF=10 /GUARD:NO`.
  The x86 Debug and ordinary x64 MinSizeRel command lines retained their
  existing settings. An x64 configuration with the x86 option enabled was
  rejected by the architecture guard.
- Ordinary x86 Debug produced the supply. The consumer embedded an identical
  header with SHA-256
  `F632FF14EC3367524B0F7A9924C156008AF1E7DF205CC2342462B6592A52C7CC`.
- Four x86 Debug checks passed for the layout solver, device state, menu
  model and full WindowCatalog model suite.
- The seven selected x86 MinSizeRel checks passed. They include the binary
  policy and both catalog consumers, layout validation including invalid
  non-finite input, device state, adaptive native chrome, and Win32
  process acceptance with saved state.
- The three selected x64 MinSizeRel binary checks passed. The ordinary x64
  catalog remains 886,272 bytes. The x86 profile produces 607,744 bytes.
- The x86 PE is a Win32 GUI image with OS and subsystem versions 6.01. Direct
  imports are from USER32, GDI32 and KERNEL32. Inspection found no direct
  imports of CreateFile2, CopyFile2, GetTempPath2W or
  GetSystemTimePreciseAsFileTime.

Actual Windows 7, Narrator speech and physical monitor disconnection were
outside this verification.
