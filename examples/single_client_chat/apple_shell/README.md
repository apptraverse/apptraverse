# Apple single-client chat

Shared SwiftUI chat UI for macOS x86_64 and iOS Simulator x86_64, backed by
the same ChatComponent / AetherP2pTransport path as Windows and Android.

The Swift Package in this directory is UI-only. It is not the product app.
`swift build` does not produce the networked Aether client.

Product builds (CMake / xcodebuild) always inject `AppleChatBackend`.

## Layout

| Path | Role |
| --- | --- |
| `Sources/AppTraverseChatAppleUI/` | Shared SwiftUI UI, view model, ChatBackend |
| `native/` | C++ Apple runtime, Objective-C++ bridge, presenters |
| `Sources/AppTraverseChatMacDemo/` | macOS product host |
| `Sources/AppTraverseChatIosDemo/` | iOS product host |
| `ios/AppTraverseChatIosDemo.xcodeproj` | iOS Simulator Xcode project |
| `CMakeLists.txt` | Product native/macOS build (standalone) |

## Native toolchain

On this Intel Mac, native C/C++/ObjC++ uses MacPorts LLVM 20:

- `/opt/local/bin/clang-mp-20`
- `/opt/local/bin/clang++-mp-20`

SwiftUI still compiles with Xcode 15.2 `swiftc`. Apple Clang 15.2 libc++ does
not provide `<stop_token>`; do not use it for Aether.

## macOS x86_64 product build

```sh
cmake -G Ninja -S examples/single_client_chat/apple_shell \
  -B examples/single_client_chat/apple_shell/.build-macos-mp20 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=/opt/local/bin/clang-mp-20 \
  -DCMAKE_CXX_COMPILER=/opt/local/bin/clang++-mp-20 \
  -DCMAKE_OBJCXX_COMPILER=/opt/local/bin/clang++-mp-20 \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 \
  -DBUILD_TESTING=OFF
cmake --build examples/single_client_chat/apple_shell/.build-macos-mp20 \
  --target AppTraverseAppleChat AppTraverseChatMacDemo
./examples/single_client_chat/apple_shell/.build-macos-mp20/AppTraverseChatMacDemo
```

State directory:

`~/Library/Application Support/AppTraverse/SingleClientChat/`

## iOS Simulator x86_64 product build

Native C++ is built with the same MacPorts clang-20 against the iOS Simulator SDK.
Swift is still compiled by Xcode 15.2. The native deployment target is iOS 16.3
because LLVM 20 libc++ `std::to_chars` is unavailable on the 16.0 simulator.

```sh
SDK=$(xcrun --sdk iphonesimulator --show-sdk-path)
cmake -G Ninja -S examples/single_client_chat/apple_shell \
  -B examples/single_client_chat/apple_shell/.build-ios-sim \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT="$SDK" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=16.3 \
  -DCMAKE_C_COMPILER=/opt/local/bin/clang-mp-20 \
  -DCMAKE_CXX_COMPILER=/opt/local/bin/clang++-mp-20 \
  -DCMAKE_OBJCXX_COMPILER=/opt/local/bin/clang++-mp-20 \
  -DCMAKE_C_COMPILER_TARGET=x86_64-apple-ios16.3-simulator \
  -DCMAKE_CXX_COMPILER_TARGET=x86_64-apple-ios16.3-simulator \
  -DCMAKE_OBJCXX_COMPILER_TARGET=x86_64-apple-ios16.3-simulator \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=OFF
cmake --build examples/single_client_chat/apple_shell/.build-ios-sim \
  --target AppTraverseAppleChat

xcodebuild -project examples/single_client_chat/apple_shell/ios/AppTraverseChatIosDemo.xcodeproj \
  -scheme AppTraverseChatIosDemo \
  -sdk iphonesimulator \
  -arch x86_64 \
  -configuration Debug \
  CODE_SIGNING_ALLOWED=NO
```

The Xcode project always instantiates `AppleChatBackend`. `FakeChatBackend` is not
part of the product target.

