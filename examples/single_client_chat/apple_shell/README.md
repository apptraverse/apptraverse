# Apple SwiftUI chat shell

Isolated exploration slice: one shared SwiftUI chat UI hosted by a macOS demo
and a runnable iOS Simulator app. No C++, Objective-C++, networking, persistence,
Æther, or ChatComponent bridge.

## Layout

| Path | Role |
| --- | --- |
| `Sources/AppTraverseChatAppleUI/` | Shared library (macOS + iOS) |
| `Sources/AppTraverseChatMacDemo/` | macOS-only executable |
| `Sources/AppTraverseChatIosDemo/` | iOS-only SwiftUI app glue |
| `ios/AppTraverseChatIosDemo.xcodeproj` | Minimal iOS Simulator app project |

Shared Swift sources (3 files, not copied):

- `ChatView.swift` — the only chat UI
- `ChatViewModel.swift` — observable model over `ChatBackend`
- `ChatBackend.swift` — `ChatBackend` protocol + `FakeChatBackend`

## Sharing

Chat UI/model/backend stays in `AppTraverseChatAppleUI`. Hosts only provide
lifecycle:

- macOS: AppKit activation + optional `--smoke`
- iOS: SwiftUI `App` + `WindowGroup` wrapping `ChatView(model:)`

## Backend protocol required by the UI

`ChatBackend` is the seam the view model talks to:

- `localUID`
- `timelineRows`
- `addPeer(uid:)`
- `submitText(_:)`
- `onChange` callback

`FakeChatBackend` only:

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

## macOS build

From this directory:

```sh
swift build
swift run AppTraverseChatMacDemo
swift run AppTraverseChatMacDemo -- --smoke
```

## iOS Simulator app build

x86_64, iOS 16 / SDK 17.2, no arm64:

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

The project references the existing shared Swift files. It does not copy them.

Install/launch needs CoreSimulatorService (`simctl`). If that service is
unavailable to the agent, the `.app` bundle can still be produced.

## Platforms

- macOS 13
- iOS 16 (Xcode 15.2 / iPhoneSimulator 17.2 SDK)
