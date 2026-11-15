# Apple native content and lifecycle extension

The generic Apple hosts now support runtime custom-content slots below activated
native windows/scenes, an optional button-content builder, input-flush and
stop-input callbacks, and an optional session-finished callback. macOS additionally
exposes session start and native-window activation for application coordinators.
The default host execution path and button appearance remain the existing ones.

Slots contain native presentation values only. Required parents are consumed as
activated graph guarantees; the API does not create, repair or serialize model
state. Each new iOS scene uses native object identity so a new session cannot
reuse a representable coordinator from a destroyed scene. Persistent graph IDs,
S0 schemas, model publication and mirror ownership are unchanged.

The C++ compile policy is restricted to CXX/OBJCXX compilation. Swift must not
receive C++ `-fno-rtti` / `-fno-exceptions` flags. C++ behavior is unchanged.

Validation on Intel macOS 13.7.8, Xcode 15.2, LLVM 20 and iOS 17.2 Simulator:

- macOS native-backend lifecycle CTest: **PASS**.
- iOS WindowCatalog Debug, embedded seed/cold, native OS deactivation, same live
  process/bridge/presenters after activation, later accepted work, and second
  background/cold restore: **PASS**.
- macOS interactive WindowCatalog: Debug and embedded-seed native scenarios
  completed through normal LaunchServices launch after desktop unlock. The cold
  model/native restore passed, but the subsequent native-click phase could not
  obtain an active/key window. Full interactive cold scope is **BLOCKED by the
  foreground precondition**, not a PASS. Earlier locked-desktop attempts are
  not regression evidence.
- The native table-input fixture now requests and waits for application/window
  foreground before posting local NSEvents. In-process events do not perform
  WindowServer's activation step associated with a real click.
- A separate integration consumer exercised custom content, button styling,
  lifecycle input barriers, separate UI mirror publication and cold USER restore.
  Its native iPhone Debug/MinSizeRel UI, keyboard rotation, background restore and
  real network round trips passed. Its macOS MinSizeRel created a visible window,
  quit through the native application lifecycle, saved USER and retained data.
- ARM64 iOS consumer build: **PASS**. Physical-device runtime: **NOT RUN**.

Windows/Linux/Android/Web runtime was not rerun for these Apple-host-only changes.
No app-specific classes or dependencies were added to AppTraverse. No main merge
or PR is part of this change.
