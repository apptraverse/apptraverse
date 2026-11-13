# Unified window order — 2026-11-11

Follow-up to `d9b4392` on `feature/cross-platform-user-state`.

## Contract

One persisted lazy-identity `window_order` contains every open window once,
including main. Index zero is active; there is no separate persisted active
window, mobile mode, or mobile navigation state. Open/Activate moves to front,
Close/Delete removes the window; closing a background window preserves active.
The stable catalog ring chooses Next/Previous independently of MRU order.
Main-window close remains the application shutdown boundary.

All hosts use one Prepare function. Desktop native activation queues the same
model action; paged hosts display order[0]. Explicit activation requests focus,
native activation reports do not replay old focus over newer native input.
Deduplication happens in the model, not against a potentially stale mirror:
queued A -> B -> A is covered. Stale activation of a closed window is ignored.
Order and pending-focus references retain identity without pinning closed graphs.

The Close control stays presenter-owned. Layout, geometry/restore separation,
RAM USER and host-specific save triggers are unchanged.

## Schema

Removed the two mobile state fields and obsolete events; common order event
replaces desktop-only order event. This intentionally updates prototype S0,
without migrations or reading old experimental profiles. Existing user profiles
were preserved. Tests regenerate matching supply/USER on all platforms.
All four new supplies share canonical SHA-256
`bee0c794ce5b33b36cf44a5423e747f1c01b8708490f0fb6e81b2219a7f469b2`.

## Validation

- Windows incremental Debug: 8/8 focused tests PASS (201.01 s), including
  model test 178.16 s, headless order/cold/replay/native-activation scenarios,
  embedded overlay, persistence boundary, coalescing and binary policy.
- Windows native acceptance and adaptive chrome: 2/2 PASS (19.93 s).
- Android x86-64 emulator: final 7/7 PASS (69.863 s); original APK restored
  byte-identical and default USER unchanged.
- Web: actual Wasm embedded test, functional UI and headed lifecycle PASS;
  lifecycle 12.765 s after correcting the old predecessor expectation.

- Four-platform seed/transfer/edit/return matrix: 52/52 PASS; 32 read-only
  USER hashes unchanged, including Android device readback.
- Actual Windows GUI imports of new Android and Web USER both PASS: imported
  active_id is native top window; caption Close, reopen, save/restart preserved.
  The native harness now explicitly checks imported active-window presentation.

- Linux incremental Debug: final 16/16 PASS (55.40 s), including unchanged
  GTK geometry/order/cold-restart assertions after the native mapping fix.

Evidence prefix:
`.artifacts/unified-window-order-*`.

Apple activation bridge and host source paths were updated; no Apple execution
is claimed on this Windows host. MinSizeRel was not rebuilt.

## Diagnostic attempts

The first Windows embedded-overlay run stopped on duplicate registry ClassId
before its scenarios. A model-test diagnostic run was interrupted before a
result was obtained. Later
relink of the overlay passed under cdb. Current static archive contains no
obsolete registration member; old event type names are absent from both EXEs.
The exact cause of the initial startup failure was not established. A running
test/debugger also temporarily blocked the linker (LNK1168); only owned test
processes were stopped. Registry checks were not weakened.

Web lifecycle originally expected the old mobile-ring predecessor after
main -> Add new child -> Close child. Unified MRU correctly returns to main;
the test now checks main identity/title immediately and after restoration.

The first matrix attempt could not execute the Android fixture after adb push
reset its executable mode. Corrected chmod 755 and reran all 52 cases against
fresh output; the passing report is `unified-window-order-matrix-final/report.json`.

Linux first acceptance ran 15/16: cold geometry passed, native order differed
from saved model. GTK restored windows initially requested focus in construction
order. Native mapping now does not request focus; the host explicitly presents
the model-selected window. Activation handlers are disconnected on unload.
The original order assertion is retained, with mismatch identity diagnostics.
