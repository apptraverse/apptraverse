# Apple SwiftUI chat shell

Isolated exploration slice: one shared SwiftUI chat UI for macOS and iOS, plus a
minimal macOS demo host. No C++, Objective-C++, networking, persistence, Æther,
or ChatComponent bridge.

## Layout

| Path | Role |
| --- | --- |
| `Sources/AppTraverseChatAppleUI/` | Shared library (macOS + iOS) |
| `Sources/AppTraverseChatMacDemo/` | macOS-only executable |

Shared Swift sources (3 files):

- `ChatView.swift` — the only chat UI; compiled for both platforms
- `ChatViewModel.swift` — observable model over `ChatBackend`
- `ChatBackend.swift` — `ChatBackend` protocol + `FakeChatBackend`

macOS-only entry (1 file):

- `AppTraverseChatMacDemo.swift` — SwiftUI `App` host, AppKit activation, optional `--smoke`

## Shared amount

- **75% of Swift files** (3 of 4) are shared.
- **76% of Swift LOC** (191 of 250) are in `AppTraverseChatAppleUI`.
- **100% of chat UI** lives in `ChatView.swift`. There is no forked iOS copy.
- Platform conditionals are only used for iOS text-input traits
  (`textInputAutocapitalization` / `autocorrectionDisabled`).

## Backend protocol required by the UI

`ChatBackend` is the seam the view model talks to:

- `localUID`
- `timelineRows`
- `addPeer(uid:)`
- `submitText(_:)`
- `onChange` callback

This slice implements only `FakeChatBackend`:

- initial row: `* Apple joined`
- Add: `* Peer added: <uid>`
- Send: `Apple: <message>`

## Expected future boundary

```
SwiftUI
  → shared Apple view model / ChatBackend
    → Objective-C++ bridge
      → C++ ChatComponent
```

That bridge is not implemented here.

## Build

From this directory, macOS 13 / Swift 5.9:

```sh
swift build
swift run AppTraverseChatMacDemo
```

CLI check of Fake Add/Send (no WindowServer required):

```sh
swift run AppTraverseChatMacDemo -- --smoke
```

iOS Simulator x86_64 compile of the shared library (Xcode 15.2 / iOS 17.2 SDK),
without a `.pbxproj`:

```sh
SDK="$(xcrun --sdk iphonesimulator --show-sdk-path)"
xcrun --sdk iphonesimulator swiftc \
  -sdk "$SDK" \
  -target x86_64-apple-ios16.0-simulator \
  -parse-as-library \
  -module-name AppTraverseChatAppleUI \
  -emit-module-path /tmp/AppTraverseChatAppleUI.swiftmodule \
  -emit-library \
  -o /tmp/libAppTraverseChatAppleUI.dylib \
  Sources/AppTraverseChatAppleUI/ChatBackend.swift \
  Sources/AppTraverseChatAppleUI/ChatViewModel.swift \
  Sources/AppTraverseChatAppleUI/ChatView.swift
```

`xcodebuild -list` in this environment did not treat the Swift package as an
Xcode package/workspace (and CoreSimulatorService was unavailable to the
agent). A runnable iOS app bundle is out of scope for this slice.

## Platforms

- macOS 13
- iOS 16 (APIs available in Xcode 15.2)
