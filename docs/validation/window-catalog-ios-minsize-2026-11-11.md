# iOS MinSizeRel size policy — 2026-11-11

User requested additional size optimization. Changes are build configuration,
test configuration and documentation only; model/USER/schema/lifecycle code is
unchanged from `f2f27c8`.

`APPTRAVERSE_IOS_MIN_SIZE_OPTIMIZATIONS` defaults ON, gated to iOS and the
single-configuration MinSizeRel build. It applies to project/dependency C-family
targets before dependency configuration:

- `-Oz`, ThinLTO, function/data sections and hidden visibility;
- C++ `-fno-exceptions -fno-rtti`, Objective-C `-fno-objc-exceptions`;
- final linker `-dead_strip` and automatic `strip -x` for product executables;
- Swift retains `-Osize -wmo` and its platform ABI.

The Clang driver supplies its matching libLTO. Compiler-reported LLVM archive
tools and explicit archive rules prevent nested `project()`/`enable_language()`
from restoring an older binutils cache. An initial attempt exposed Apple LLVM15
ranlib reading LLVM20 bitcode; corrected rules use LLVM20 throughout and link
successfully. No global exception/unwind removal, unchecked Swift optimization
or altered floating-point semantics was added. Other platforms and Debug do not
receive the size policy.

## Measurements

Intel x86_64 iOS Simulator17.2; MacPorts Clang20.1.8; Xcode15.2/Swift5.9.2.
These are executable/bundle file lengths, not installed device footprint.

| Build | Executable bytes |
|---|---:|
| Earlier ordinary MinSizeRel, unstripped | 1,259,072 |
| Earlier binary after local-symbol removal only | 815,752 |
| Optimized fixture-enabled product | 591,280 |
| Final optimized product, test access disabled | **540,376** |

Final `.app`: **541,584 bytes**. Reduction from ordinary unstripped MinSizeRel:
718,696 bytes (57.1%). The final embedded header is byte-identical to the
Debug-prepared revision (`1c884717301fd3d79155701ed69913448b863d6f412cbfe3728f2dc88b7bc3a4`).

## Verification and limits

Optimized native fixtures: **5/5 processes PASS**, with the existing 23/23/5
checkpoint runs and real Settings/deactivation, same-PID/bridge/presenter resume,
newer model work, second save, process termination and 5-checkpoint cold load.
Under MinSizeRel both fixture variants are embedded consumers with fresh test
profiles; this is not a normal Debug-producer run. Swift test access is explicit
when `APPTRAVERSE_BUILD_IOS_ACCEPTANCE=ON`.

Acceptance was then disabled and the final product rebuilt (3/3 steps). Its
stripped binary was copied unchanged to a separate test bundle identity, leaving
the user's installed product and profile intact. It cold-loaded a matching test
USER, rendered localized Window3, wrote USER after actual OS deactivation and
resumed with the same PID76121. This final product smoke is distinct from the
full fixture scope. The temporary test process was terminated and the user's
existing WindowCatalog brought back to foreground.

Symbol inspection found no application graph builder/distillation, C++ throw
or dynamic-cast imports. Generated commands verify the intended flags and
matching LLVM20 archive tools. Syntax/whitespace checks PASS.

Logs: `work/ios-size-opt-{configure,build,tests}.log`,
`work/ios-size-opt-final-{configure,build}.log`,
`work/ios-size-opt-acceptance/20261111-185502`,
`work/ios-size-opt-smoke.log` and its JSON record/screenshot.
Physical ARM64 iPhone, device signing and other-platform runtime runs NOT RUN.
