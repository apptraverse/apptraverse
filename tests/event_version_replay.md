# Event version matrix: replay vs load

`apptraverse_event_version_matrix` exercises the same ClassId v0/v1 contract as
before, and additionally requires `TryReplayFromBase()` to rebuild materialized
fields from the persisted base snapshot plus journal.

- `reload` / `inspect`: `Load()` only (materialized checkpoint + journal bytes).
- `replay-from-base`: load, then `TryReplayFromBase()` twice (idempotent).
- `replay-from-base-fault`: corrupt tally/extension in memory, then replay.
- `export-v1-package`: writes document **and** base snapshot v1 layers
  (`extension=640`) without touching user v0 layers.

Baseline sub-run (`baseline_replay_doc_only_v1` in test stdout) installs
document v1 only; replay typically yields `tally=17` but `extension!=645` because
the base snapshot lacks packaged v1 extension.
