# A1R1 investigation artifacts (non-shipping)

Diagnostic-only code and logs from the P3.T3.A1R1 opener-label investigation.
Not linked into product targets (`apptraverse`, Android JNI, Win/WASM demos).

- `include/`, `src/` — `PublicationOpenerLabelTrace`, lifetime hooks, ObjPtr `--wrap` trace
- `patches/aether-objects-a1r1-opener-lifetime-diag.patch` — weak hooks in aether-objects (removed from CMake patch chain)
- `gen_a1r1_diag_patch.py` — regenerates the diag patch against pin `79283b6` + v6 + v6r1

Saved logcat / LLDB captures remain under `tools/a1r1_*.txt`.

**Shipping fix (v6r1):** `cmake/patches/aether-objects-reset-object-load-state-v6r1.patch` on top of `aether-objects-cold-load-root-flush-v6.patch`.
