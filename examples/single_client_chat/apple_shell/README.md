# Apple single-client chat

Shared SwiftUI chat UI for macOS x86_64 and iOS Simulator x86_64, backed by
the same ChatComponent / AetherP2pTransport path as Windows and Android.

## Layout

| Path | Role |
| --- | --- |
| `Sources/AppTraverseChatAppleUI/` | Shared SwiftUI UI, view model, ChatBackend |
| `native/` | C++ Apple runtime, Objective-C++ bridge, presenters |
| `Sources/AppTraverseChatMacDemo/` | macOS host |
| `Sources/AppTraverseChatIosDemo/` | iOS host |
| `ios/AppTraverseChatIosDemo.xcodeproj` | iOS Simulator Xcode project |
| `CMakeLists.txt` | Product native/macOS build (standalone, not added by root CMake) |

macOS and iOS share ChatView, ChatViewModel, ChatBackend, the ObjC++ bridge,
and the C++ runtime.

## Backend seam

SwiftUI talks only to `ChatBackend`:

- `localUID`
- `timelineRows`
- `addPeer(uid:)`
- `submitText(_:)`
- `onChange`

Product hosts inject `AppleChatBackend` → `ATAppleChatBridge` →
`AppleChatRuntime` → `ChatComponent` → `AetherP2pTransport`.

## macOS x86_64 build

```sh
cmake -G Ninja -S examples/single_client_chat/apple_shell \
  -B examples/single_client_chat/apple_shell/.build-macos \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DBUILD_TESTING=OFF
cmake --build examples/single_client_chat/apple_shell/.build-macos \
  --target AppTraverseAppleChat AppTraverseChatMacDemo
```

State directory:

`~/Library/Application Support/AppTraverse/SingleClientChat/`

## iOS Simulator x86_64

Build the native framework with `CMAKE_SYSTEM_NAME=iOS` and
`CMAKE_OSX_SYSROOT=iphonesimulator`, then:

```sh
xcodebuild \
  -project ios/AppTraverseChatIosDemo.xcodeproj \
  -scheme AppTraverseChatIosDemo \
  -configuration Debug \
  -sdk iphonesimulator \
  -arch x86_64 \
  -derivedDataPath .derivedData \
  CODE_SIGNING_ALLOWED=NO \
  CODE_SIGNING_REQUIRED=NO \
  ONLY_ACTIVE_ARCH=YES \
  EXCLUDED_ARCHS=arm64 \
  build
```

## Toolchain note

Pinned Aether requires C++20 `<stop_token>` and designated-initializer CTAD
that Apple Clang 15.0 / macOS SDK 14.2 libc++ does not provide. See
`native/compat/stop_token` for the missing-header shim. The remaining Aether
compile failure is `aether/executors/scheduler_on_tasks.h`.
