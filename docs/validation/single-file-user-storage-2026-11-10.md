# RAM USER with one shutdown file вЂ” 2026-11-10

Base: `679b9bed950bab72d91a2c5e0e1d93d3860f0f58`.

## Contract

USER is an ordinary RamDomainStorage throughout the application session. Startup explicitly reads `state-dir/user.bin` into RAM. The existing USER-over-BACKING overlay and Domain APIs are unchanged. Child Close saves the unloaded window and its private presenter state into RAM only; reopening resolves lazy objects from that RAM table. Application shutdown first saves the remaining model graph to RAM, then explicitly writes the entire USER table to the single file.

`SingleFileUserStorage` uses the existing BinaryArchive/BinaryVectorBuffer serializer. It stores object IDs, class/version layer maps, deletion tombstones and object-scope metadata (including explicit default scopes). The file has its own magic/version header; application S0 schemas are unchanged. BACKING is not packed into USER. Save writes a temporary file and replaces `user.bin` after successful write/close. No destructor flush or background writer exists. File-load/file-save failures propagate as application failure.

Integrated into WindowCatalog's common desktop lifecycle and Windows MinimalApp. Normal Debug WindowCatalog still distills fresh supply and uses a fresh debug-run USER; its BACKING is loaded into RAM before the interactive session. Embedded consumers load BACKING into RAM as before. Linux common source is updated but not executed here; other platform-specific hosts remain outside this Windows task. DirectoryDomainStorage remains available for distillation, library tooling and existing explicit directory-based tests.

Existing experimental per-object USER directories are not imported, deleted or migrated. A missing `user.bin` starts with empty USER over BACKING. Normal Debug still intentionally does not restore a previous run's USER.

## Evidence and tests

- Storage test: binary round-trip of layers/multiple versions/scopes/tombstones; no disk directory before explicit save; exactly one file after save; second save replaces the file; unsaved RAM and destructor are invisible to disk readers; malformed load leaves RAM unchanged; failed file save is reported.
- Catalog boundary test: forty geometry events; no files after Add/resize/child Close; lazy private presenter and geometry can reload into a fresh Domain from RAM. Exactly one file after shutdown; fresh disk load restores changed geometry, locale and presenter state. A child left closed at shutdown also reloads completely from the file.
- Existing USER boundary: child Close does not change file bytes or timestamp. Two simultaneous sessions keep independent RAM USER stores.
- Windows acceptance tests now read/write the file-backed RAM table for cold probes and negative fixtures. They still exercise the actual application processes and their existing restoration/schema checks.

## Execution results

- Debug: 84/84 non-timing CTest entries PASS, then Close diagnostic PASS (85 total). Logs: `.artifacts/single-user-full-build.log`, `single-user-full-tests.log`, `single-user-debug-timing.log`.
- MinSizeRel: 4/4 storage/boundary/WindowCatalog/MinimalApp acceptance entries PASS. Logs: `.artifacts/single-user-min-build.log`, `single-user-min-storage-tests.log`, `single-user-min-acceptance.log`.
- Normal MinSizeRel executable: 20/20 child-Close observations and 20/20 app-exit observations with an open child PASS. Saved main-open scenario requires actual child restoration, with no Add fallback. All process exits zero.
- Debug keeps assertions enabled with `AE_NO_DEBUG_LOG=ON`. No builds or other UI tests ran during timing.

| Child HWND destruction median (5 repeats), ms | Previous directory USER Debug | RAM USER Debug | Previous directory USER MinSizeRel | RAM USER MinSizeRel |
|---|---:|---:|---:|---:|
| Cold Add/Close | 521 | 116 | 395 | 23 |
| Main resize burst, immediate child Close | 991 | 148 | 709 | 42 |
| Main resize burst, stable geometry then Close | 958 | 157 | 663 | 15 |
| Runtime-prepared saved profile | 543 | 111 | 369 | 13 |

Debug uses the embedded load-only executable for comparable persistence scenarios. Normal Debug still prepares a fresh graph every launch. Stable geometry is not a queue-drain guarantee. Polling interval is 10 ms; the before/after samples are sequential runs, not an isolated profiler attribution.

MinSizeRel whole-process exit with one child still OPEN, including final USER file save: medians 45 ms fresh, 48 ms after main resize burst, 55 ms after child resize burst, 43 ms for restored saved profile. Debug main exit AFTER the child was already closed: 46–47 ms fresh/burst, 61–62 ms saved.

Probe runs under `Documents/Codex/.../minsizerel-close-check`: `standard-4788dcad45c6470f838908a8bb630891` and `main-open-90d9b3d4dfbe48f3933c63cc267e90c8`. Both verify unchanged executable SHA256 `BCF85677FC55B8E148CF79869E39A3D010D09EDFA58DB6294B85B8BE15DF5305`. An example one-child USER file is 4,706 bytes; this is scenario-dependent, not a size limit.
