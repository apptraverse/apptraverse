# Same-class version layers and EXE rollback

Three independent EXEs use exactly the same named Window ClassId; no Window inheritance.

- A: Window v0 reads/writes x and y.
- B: Window schema v1. Load/Save(v0) handles x/y only;
  Load/Save(v1) handles width/height only. Both operations are independent.
- C: Window schema v2. Load/Save(v2) handles border_width only; still loads v0/v1.
- B produces a serialized *package* with the Window v1 layer (640x480).
- C produces a serialized *package* with the Window v2 layer (border_width=4).
- The runner copies packaged layers into a v0 profile without overwriting present slots.
- Golden SHA256 for initial A v0, B package v1, and C package v2 layers are fixed
  constants in `window_schema_layers_matrix.py` (same bytes in Debug and Release).

## Verified contract (requires native CTest run)

1. A writes x=10,y=20 in a fresh state directory. B refuses to inspect it
   until the separately serialized initial v1 layer has been supplied.
   Inspect/refusal must not alter any state bytes.
2. B's package creates a native serialized v1 slot, but no Window v0 slot.
   Installation preserves v0 bytes exactly and adds v1 without rewriting v0.
3. Both EXEs retain ObjId and ClassId. A reads only x/y, B reads both layers.
4. User mutations use `SaveRoot` on a fresh `DomainGraph` after load (load uses
   its own graph). Only the B `package` op writes v1 via `SaveVersion<1>`.
   An A or B move changes only the v0 bytes, preserving v1 SHA256.
   A B resize changes only v1, preserving v0 SHA256. SaveRoot may invoke both
   serializers; the contract is byte-stable untouched layers, not skipped saves.
5. B `move-resize` (SaveRoot) updates both layers; a follow-up A move updates
   only v0 while v1 stays byte-identical (23,37,740,530 then 30,46,740,530).
6. Repeated B-A-B transitions use new processes and must retain the
   latest position and size independently.
7. `three-version-rollback`: after B supplies v1 and resizes, C refuses until v2
   is packaged and installed; A/B/C mutations touch only their layer bytes
   (final A 17,29; B adds 840x580; C border 8).
8. The package source and executable hashes must remain unchanged;
   results.json and process logs show value or byte-level failures.
9. `post-load-reconcile`: rule x+width=1000 is applied only by explicit fixture
   `reconcile` on B/C after LoadRoot; serializers and Load(v0/v1/v2) never
   rewrite width automatically (not production startup behavior).
10. `backing-overlay-three-version`: USER v0 plus BACKING v1/v2 (copied from
   separate B/C packages); mixed overlay reads and per-layer user writes through
   `--backing-dir` without copying packages into USER.
11. `backing-overlay-untouched-layer`: SaveRoot through overlay must not create
   a USER layer when serialized bytes match BACKING; only changed layers (e.g.
   v2 border) are materialized while v1 stays on BACKING alone. After a simulated
   supply refresh (new backing v1 at 740x530), C still reads updated v1 from
   backing without materializing USER v1; user v0/v2 overrides remain.

## Run

With APPTRAVERSE_BUILD_EVOLUTION_TESTS=ON:

    cmake --build <build-dir> --target apptraverse_window_schema_layers_fixtures
    ctest --test-dir <build-dir> -R '^apptraverse_window_schema_layers_roundtrip$' --output-on-failure

On multi-config generators use --config Debug and ctest -C Debug.

The package-creation step generates a native serialized state in a separate
directory to emulate distribution of new initial state with an EXE. It tests
serialization and rollback semantics, not a real installer/update pipeline.
No AppTraverse working class or serialization implementation is modified.
The earlier application_evolution_matrix is retained as a distinct test of
schema changes that duplicate old fields and produce native rollback failures.
