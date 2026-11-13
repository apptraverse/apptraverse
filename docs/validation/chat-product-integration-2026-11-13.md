# Chat consumer integration

Base: `68233585` (host/client roles and UID-only discovery). The supplied chat
consumer API extension was applied on top without dropping those changes:
structured message records, a durable platform flush callback before business
ACK, and an asynchronous local-commit result for clearing the editor.

There is one current application schema. The user explicitly excludes reading
old profiles and backward-compatibility/migration work. Tests use fresh profiles
and restore only USER created by this same build; no S0-to-S1 adapter is present.

Executed Windows x64 Debug acceptance with the canonical chat source snapshot
and SDK `cc8155d2`:

- Consumer and six requested targets build PASS (`integration-chat-build-v3`).
- Model, native window lifecycle and native controls: 3/3 PASS
  (`integration-chat-tests-v3`).
- Real Aether TCP, two production chat model/runtime workers: Unicode both ways,
  kill one peer, two messages committed while it is absent, restart and exact
  ordered history without duplicates, kill both, same room and stable message
  IDs on cold restore, another message after restart, clean exits: PASS
  (`integration-chat-recovery-v2`, 10.219 s including cleanup).

This is Windows integration evidence, not Android/Web/Linux revalidation or a
Presence/throughput benchmark. The new adaptive stream is not selected for
SharedNode. Server-dependent Presence acceptance remains on hold.

The first build failed because the external integration runner replaced CMake's
MSVC defaults while adding `/MP4`, dropping `WIN32`. Correcting the runner to
retain defaults fixed the link; no socket initializer workaround was applied.
An intermediate migration experiment was removed before the final v3/v2 checks.
