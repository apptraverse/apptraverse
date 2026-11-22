# SwiftUI custom session host — 2026-11-22

Base: main d53ec01ff77a8caa717eaa69bd8a8e4b5e217efe. macOS 13.7.8 on Intel,
Xcode 15.2 / SDK 14.2, LLVM 20; downstream MinSizeRel consumer.

Added generic SwiftUI custom-control mounting in the common constraint layout,
a root view for an embedding application's scene, per-session completion and
input-stop hooks, and explicit pre-mount teardown. Native-window ownership rejects
restored duplicate scene capabilities. MacSceneWindowActions closes an unbound
SwiftUI scene through the permitted native-window boundary on macOS 13. These
APIs contain no product model or transport types.

Limited the no-RTTI/no-exceptions compile options to CXX/OBJCXX; Swift cannot accept
those flags. The representative actual C++ compile command before/after the guard
was identical. No C++ implementation or other platform adapter changed.

Executed evidence:

- Complete downstream MinSizeRel SwiftUI consumer compile/link: PASS.
- Real signed app first launch and cold relaunch: PASS. Each had one visible
  window, ordinary Quit, normal child exit 0 and a nonempty persisted USER.
- Downstream model/registry CTest: 2/2 PASS.
- Product patches applied to a clean pinned tree and matched all built sources.
- Four downstream native XCTest cases compiled and the runner was signed, but the
  UI run stopped before any method: testmanagerd requires authentication to enable
  automation mode. UI input, custom control hit testing, launcher close/replacement
  and multiple-window interaction are BLOCKED / not claimed as executed PASS.

The downstream process smoke covers startup and teardown, not the entire public
API surface. WindowCatalog, MinimalApp, iOS and other backend runtime matrices were
not re-run in this task; historical reports keep their original scope. No
AeroAdmin dependency or implementation was added to AppTraverse.
