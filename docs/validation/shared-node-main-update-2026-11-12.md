# SharedNode upstream main update — 2026-11-12

Merged origin/main 0c272079 into feature/shared-node-native-memory, preserving both PROGRESS entries. Dependency revisions fetched from upstream main:

- aether-client-cpp: 6b7abc458a86b93b2ba3b5e92ee19f05e5101757 (clean upstream; previous frozen presence/registration patches are no longer applied).
- aether-tele: 64183232c1c6d8ad6119f75ff7e833f43467de57.
- Objects remains the existing pinned 79283b6 with the complete AppTraverse patch chain, including main's domain ID sequence and releasing-edge patches. Built from a separate checkout; shared caches were not modified.

## Executed

Incremental Windows MSVC Debug build and CTest: **10/10 PASS**, 10.99 seconds. Includes network LocalPtr, memory link, SharedNode memory, sync ACK/retry, graph import, three peers, native object graph serialization, object clone, domain hierarchy and releasing edge. Evidence: `.artifacts/main-update-core-build.log`, `.artifacts/main-update-core-ctest.log`.

## Blocked integration

Full adapter build **FAILS** in new upstream SDK `aether/tele_statistics.cpp:24`: missing `aether-objects/telemetry.h`. The SDK now requires a newer Objects API than AppTraverse's pinned version. Updating Objects and reconciling its native serialization patch chain is still required.

SDK tests configuration with APPTRAVERSE_BUILD_AETHER_SDK_TESTS=ON also fails because upstream main does not contain tests/test-presence-api/main.cpp. Client::IsLocallyOnline, Client::QueryPeerPresence and the old native presence headers are absent, while our optional adapter still references them. The core run explicitly used SDK tests OFF; this does not qualify presence or SDK suites as passed.

No new cloud, chat GUI or latency run is claimed. Old executable results were not used as new validation. Next integration work: reconcile Objects telemetry API and adapter's obsolete optional presence dependency, then rebuild and run SDK/adapter/cloud/chat checks. Evidence: `.artifacts/main-update-configure.log` (final core-compatible configure), `.artifacts/main-update-adapter-build.log` (SDK compilation failure).
