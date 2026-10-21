# Same-class version layers and EXE rollback

Two independent EXEs use exactly the same named Window ClassId; no Window inheritance.

- A: Window v0 reads/writes x and y.
- B: Window current schema v1. Load/Save(v0) handles x/y only;
  Load/Save(v1) handles width/height only. Both operations are independent.
- B produces a serialized *package* state with the Window v1 layer (640x480).
  Its Load(v0) neither supplies size defaults nor migrates v0 to v1.
- The runner copies only the packaged version-1 layer into a new v0 profile.
  Existing user position is retained; the runner never overwrites a present v1.

## Verified contract (requires native CTest run)

1. A writes x=10,y=20 in a fresh state directory. B refuses to inspect it
   until the separately serialized initial v1 layer has been supplied.
   Inspect/refusal must not alter any state bytes.
2. B's package creates a native serialized v1 slot, but no Window v0 slot.
   Installation preserves v0 bytes exactly and adds v1 without rewriting v0.
3. Both EXEs retain ObjId and ClassId. A reads only x/y, B reads both layers.
4. An A or B move changes only the v0 bytes, preserving v1 SHA256.
   A B resize changes only v1, preserving v0 SHA256.
5. Repeated B-A-B transitions use new processes and must retain the
   latest position and size independently.
6. The package source and executable hashes must remain unchanged;
   results.json and process logs show value or byte-level failures.

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
