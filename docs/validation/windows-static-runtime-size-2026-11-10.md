# Windows static runtime and binary size — 2026-11-10

Scope: WindowCatalog and MinimalApp on Windows x64, with their headless tests.
Baseline: `5200c0407e960a9430c81a4da09742664c729cd6`, branch
`feature/domain-hierarchy-cloning`. Existing Debug and MinSizeRel build trees and
the previously verified dependency source were reused. No clean/populate or
other-platform build was performed.

## Implementation

- Set `CMAKE_MSVC_RUNTIME_LIBRARY` before creating any dependency targets.
  Debug retains `/MDd`; Release, MinSizeRel and RelWithDebInfo use `/MT`.
  AppTraverse, the Windows backend and Aether objects are static libraries.
  The final PE imports prove that CRT/STL and project runtime DLLs are absent.
  See [CMake's runtime selection](https://cmake.org/cmake/help/latest/prop_tgt/MSVC_RUNTIME_LIBRARY.html).
- Enable function/data COMDAT packaging (`/Gy`, `/Gw`, `/GF`) and
  `/OPT:REF /OPT:ICF=5` in non-Debug configurations. MinSizeRel retains its
  existing `/O1 /GL /LTCG` policy. No `/WHOLEARCHIVE` was present or added.
  [MSVC documents the elimination/folding rules](https://learn.microsoft.com/en-us/cpp/build/reference/opt-optimizations?view=msvc-170).
- Separate core registration from the full UI registration entry point.
  Windows applications explicitly register their window/button/native-list
  controls. The existing full registration APIs remain available to other
  consumers and tests. Closed-event serialization no longer pulls the UI schema
  into the core implicitly.
- Move standalone `UiListRowPresenter` and `WinWindowPresenter` registrations
  into separate archive members. Windows native lists have no per-row presenter;
  both applications use `WinDesktopWindowPresenter`. WindowCatalog's desktop
  registration no longer forces the mobile in-content `CatalogCloseButton`.
  All required model, event and base-class registrations remain.
- Separate the Windows catalog graph builder from presenter registration and
  methods. Separate MinimalApp bootstrap and legacy directory-persistence
  functions from current runtime persistence. Remove the unused global
  `AE_DISTILLATION` definition; scope the MinimalApp distillation definition to
  its bootstrap source on Windows. Preserve the other platforms' existing
  bootstrap selection.
- Add four non-Debug binary-contract CTests, covering both normal applications
  and both load-only consumers. They inspect normal/delay imports and MSVC maps,
  rejecting dynamic runtime DLLs, distillation/builders/directory-storage object
  members, and the three unused concrete factories.
- Non-Debug native acceptance now starts the normal shipped application for
  interaction and restart scenarios. Debug still tests its fresh-graph behavior
  plus the explicit load-only consumer.

No persistence format, application schema, Node history/replay, model behavior,
or presenter lifecycle was changed. Debug still prepares the embedded supply.
Release contains that supply's data and the reader, without the producer code.

## Controlled size comparison

Fresh MinSizeRel builds, x64 MSVC 14.50.35717, `AE_NO_DEBUG_LOG=ON`.
Sizes are on-disk PE lengths; 1 KiB = 1,024 bytes.

| Configuration | WindowCatalog | MinimalApp |
|---|---:|---:|
| Current baseline, dynamic CRT (`/MD`) | 492,544 B / 481 KiB | 421,376 B / 411.5 KiB |
| Static CRT only, before pruning | 709,632 B / 693 KiB | 641,024 B / 626 KiB |
| Final static runtime and pruned linkage | **684,544 B / 668.5 KiB** | **622,592 B / 608 KiB** |
| Savings versus static-CRT-only build | **25,088 B / 24.5 KiB** | **18,432 B / 18 KiB** |
| Net change versus dynamic-CRT baseline | +192,000 B / 187.5 KiB | +201,216 B / 196.5 KiB |

Static CRT alone adds 212 / 214.5 KiB. The final executable is larger than the
dynamic baseline because it now carries the runtime that previously came from
DLLs. The pruning savings include registration/source separation and COMDAT
data elimination; they are not attributed entirely to one class.

This comparison does not reconstruct the older approximately 350 KB executable:
that artifact/configuration was not used as the baseline for this task.

## What occupies the final binaries

Exact PE raw section sizes:

| Section | WindowCatalog KiB | MinimalApp KiB |
|---|---:|---:|
| `.text` — machine code | 523.5 | 472 |
| `.rdata` — read-only data, tables, imports | 101.5 | 97 |
| `.data` — initialized writable data | 8 | 7.5 |
| `.pdata` — x64 unwind metadata | 28.5 | 25 |
| `.reloc` | 5 | 4.5 |
| `.rsrc` | 0.5 | 0.5 |
| `.fptable` | 0.5 | 0.5 |
| PE headers | 1 | 1 |
| **Total** | **668.5** | **608** |

WindowCatalog code ownership estimated from map address intervals:

| Linked library/group | Approximate code KiB |
|---|---:|
| Static CRT/STL (`libucrt`, `libvcruntime`, `libcpmt`, `LIBCMT`) | 148.2 |
| AppTraverse core and UI schema | 143.5 |
| Catalog common model/schema/actions | 142.8 |
| Aether objects | 23.4 |
| Application entry and model session | 19.1 |
| Catalog Windows adapter | 17.2 |
| Common Windows backend | 16.8 |
| Shared/folded code with multiple owners | 12.5 |

These are estimates, not exact function sizes: ranges include padding, inlining
and linker-selected template instantiations. Folded aliases are counted once,
with ambiguous ownership reported separately. In particular, moving a template
instantiation between object files does not mean its runtime behavior vanished.

The largest project source contributions are catalog core registration (~63.4
KiB), common UI registration (~60.9 KiB), catalog model (~40.1 KiB), cloning
(~22.8 KiB), serialization (~18.7 KiB), and catalog event registration (~17.2
KiB). Registration retains per-class factories, serializers and graph visitors;
the required class hierarchy and history/replay still need these functions.
`CloneCanonicalRoot` is the largest individual named code interval (~5.8 KiB).

The static CRT includes generic formatting, locale and floating-point conversion
routines/tables. Examples in the map include `__acrt_fltout` (~4.6 KiB code),
locale name tables (~7.1 KiB combined), and vector-algorithm lookup tables.
Their presence alone is not evidence of an unreferenced object being linked:
generic runtime entry points can retain their internal dispatch paths.

Embedded state is a small part of the total: catalog layer payloads sum to
2,244 bytes; its 230 layer records occupy 7,360 bytes, plus scope and supply-ID
tables (roughly 10.5 KiB altogether before linker padding/folding). MinimalApp
payloads sum to 2,132 bytes with 238 layer records. Embedded data remains
necessary even though its distillation code is absent.

## Linkage evidence

- Both final normal EXEs import **only `KERNEL32.dll` and `USER32.dll`**.
  `dumpbin /dependents` independently agrees with the Python PE reader.
- Final maps contain no distillation/export object files or initial graph
  builders; no directory-storage implementation, standalone row/window factory,
  or mobile Close-button factory is linked into these Windows consumers.
- The baseline already discarded the named Build/Distill entry points. It still
  selected graph-builder archive members supplying shared template instances,
  and retained unused concrete factories through aggregate registration. This
  was not evidence that the baseline executed distillation at runtime.
- The binary contract rejects the pre-pruning static-CRT catalog for its builder
  members and all three unused factories. The dynamic baseline additionally
  exposes MSVCP/VCRUNTIME/UCRT imports. The final four consumers pass.
- No dynamic CRT compilation flag remains in the MinSizeRel Ninja rules.
  Static runtime selection also applies to Release/RelWithDebInfo; separate
  builds of those configurations were **not run**.

## Validation

- **82/82 Debug CTests PASS**, 195.19 seconds, assertions enabled.
- **17/17 targeted MinSizeRel CTests PASS**, 43.69 seconds on the final link.
  This includes four binary contracts; clone/scope/single-file storage;
  application-contract headless; both application headless/export suites;
  catalog overlay and persistence boundary; both unload disk round-trips;
  and both native acceptance suites (interaction, save, cold restart).
- Debug supply generation succeeds through the Debug applications themselves.
- `git diff --check` passes.

The first test run used the execution environment's inaccessible temporary
directory and produced file-write failures. It was stopped and is not counted
as validation. The final runs use separate writable `.artifacts/td` (Debug) and
`.artifacts/tm` (MinSizeRel) temporary roots. No user profile was used.

During incremental Debug relinking, MSVC reported stale incremental-link
`LNK1236` errors after the archive member split. Affected links succeeded after
relinking without the incremental linker (`_LINK_=/INCREMENTAL:NO` for that
invocation); no clean build or dependency replacement was needed. A Windows
`RegisterClass` macro/include-order error in the new full-registration member
was fixed before the passing builds. There is no outstanding test failure.

## Reproduction and artifacts

With the existing MSVC developer environment, build the normal/load-only apps
and the targeted test executables in `build/win64-ninja-msvc-minsizerel`.
Set `TEMP` and `TMP` to writable, separate test directories, then run:

```powershell
ctest --test-dir build/win64-ninja-msvc-debug -C Debug --output-on-failure
ctest --test-dir build/win64-ninja-msvc-minsizerel -C MinSizeRel -L binary_contract --output-on-failure
python scripts/inspect_windows_binary.py build/win64-ninja-msvc-minsizerel/examples/surfaces_demo/windows/win32_window_catalog_demo.exe --runtime-only --json catalog-size.json
```

The script also works without `--runtime-only` for baseline inspection. Maps
are generated next to each non-Debug application. Local ignored evidence is in
`.artifacts/release-size/`: baseline and static-CRT-only EXE/map snapshots,
`catalog-final.json`, `minimal-final.json`, `negative-contract.log`,
`debug-ctest-final.log`, `min-ctest-final.log`, and build logs. Binaries are build
artifacts and are not committed to Git.
