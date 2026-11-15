# Push service moved into AppTraverse

Copied the existing Java Aether/FCM push service, protocol, unit tests and pinned Gradle wrapper from `feature/aether-fcm-push` into AppTraverse main. The service is an application-owned Aether client; no Aether library sources are modified. Windows verification and end-to-end Firebase evidence are recorded in `push-review-2026-11-14.md` on the source branch.

Linux uses the published Hydrogen native library. The macOS Gradle build selects WebRTC by CPU architecture and builds pinned libhydrogen through CMake; the launch script points Java at the bundled library. No Apple Push Notification provider is implemented; this is the integration point for the Apple consumer.

This Windows workspace has only JDK 20, while the pinned Aether Java SDK requires JDK 25. Java tests and Linux/macOS runtime checks were not rerun here. The platform build and runtime must be verified on those hosts before declaring platform acceptance.

Executed here: C++ protocol fixture and stream framing PASS. Gradle Java test blocked at compileJava: invalid source release 25 under installed JDK 20.
