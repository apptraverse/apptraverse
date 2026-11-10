# Windows and headless architecture principles — 2026-11-10

Branch: `feature/domain-hierarchy-cloning`. Base: `2dadc6b83ac260b5e2a66c95017a4a8171b52c00`.
Scope: incremental Windows Debug, common model code and host-side headless tests. `AE_NO_DEBUG_LOG=ON`; Debug assertions remain enabled. No MinSizeRel or device/platform build was run in this iteration. Adaptive WIP was not modified.

## Implemented contract

- **Explicit graph construction.** Persistent window/client geometry, locale, button enabled state, list selection, edit-box state, bitmap dimensions, catalog counters and viewport are assigned by builders/fixtures. Loading no longer repairs missing desktop restore dimensions. Runtime flags retain normal initialization. Event payloads and library history mechanisms are not removed.
- **Required UI graph.** Windows/common window title and list presenters no longer silently accept missing required links. Catalog presenter mixins no longer fall back to unrelated base rendering when their catalog link is missing. Seven Windows fixtures now explicitly construct localized titles and locales, including an intentionally empty title. Localized format strings such as `Window {0}{1}` remain supported.
- **Language residency.** Immediate compaction remembers originally lazy language slots. Direct historical capture uses the same operation-local preservation hooks. Both restore the original active/inactive residency after temporary capture materialization. The regression also checks inactive text, switching languages and unchanged storage.
- **Automatic cloning.** Removed the public `CloneOptions::boundary_ids` field. Tests express external references using parent domains, constant scope metadata or existing overlay constants. Internal automatic boundary bookkeeping remains. Catalog loads the source prefab into a temporary child domain and releases it through normal graph ownership; no application-level `RemoveObject` enumeration remains in Catalog/MinimalApp common code.
- **One RAM storage implementation.** Removed `SingleFileUserStorage`. USER, BACKING and scratch buffers use ordinary `ae::RamDomainStorage` instances. `LoadUserStorageFile` / `SaveUserStorageFile` are explicit functions. Child Close serializes into RAM; application shutdown writes `user.bin`. No destructor/background flush exists.
- **Prototype file contract.** The file contains the existing binary layer table, tombstones and scope metadata. Removed the container magic/version, extra scope validation and temporary-file replacement. Ordinary first-run absence and I/O/archive results are still handled. Experimental older file/schema formats are not migrated; use fresh profiles for this revision.
- **Initial placement.** Catalog's supplied graph and new children request OS placement, without a fabricated cascade. The Windows presenter reports actual initial geometry through the existing model event path and reports monitor work-area bounds through `ReportUiDesktopScreenEvent`. Fixed frame requests and centered placement are model behavior. Native observations become explicit saved geometry; a cold USER load restores it. Screen coordinates may be negative. Centering is applied once and emits a frame publication request; later screen reports do not recenter an already placed window.
- **Platform boundary.** Windows only measures native geometry/work area and applies model requests. The headless model test verifies OS-default state, actual-position events, independent-domain cold restoration, centered placement and its one-shot behavior. The Android-graph headless test now generates its supply with the existing Windows host exporter into the build tree, avoiding a stale checked-in schema; this is not Android device acceptance.

## Validation

Final source tree: **84/84 ordinary CTest entries PASS**, followed by the separate Close diagnostic **1/1 PASS**, including **20/20 child Close observations**. No builds or other GUI tests ran during timing. A separate stage probe passed with RAM model Close **4 ms cold / 6 ms burst**, and zero disk bytes. Its directory-storage controls were **310 / 489 ms**; those controls are not the application's USER path.

Earlier iterations exposed seven incomplete Windows fixtures, an omitted Surfaces locale initialization, obsolete cascade-coordinate assertions, and stale embedded supply in the Android headless fixture. These were corrected rather than bypassed or disabled. All 85 configured tests remain enabled.

Logs: `.artifacts/principles-accepted-build.log`, `principles-accepted-tests.log`, `principles-accepted-timing.log`, `principles-close-stages.log`.

| Scenario | HWND destroy median, ms | List median, ms |
| --- | ---: | ---: |
| COLD_ADD_CLOSE | 204 | 190 |
| BURST_CLOSE_IMMEDIATE | 208 | 189 |
| BURST_CLOSE_AFTER_SETTLE | 216 | 194 |
| SAVED_PROFILE_CLOSE | 118 | 109 |

Main-process exit after child Close: **45–78 ms**, all exit codes zero. OBJ_SYS output: zero. HWND hiding is not a separately observed stage. Stable HWND geometry is not a proven publication drain.

The previous single-file Debug run recorded 116/148/157/111 ms; this run recorded 204/208/216/118 ms. This is a higher observed latency for fresh windows, not evidence of a performance improvement. The supply and initial-event path changed, and this iteration did not perform a controlled old/new executable comparison or CPU attribution. The model-only RAM result rules out treating all observed HWND latency as model persistence or disk cost; queue/publication/UI contributions remain unseparated.

## Reproduction

Use the existing Windows Debug build with the configured verified dependency source. Build the existing Windows CTest executable targets incrementally (including the demo/load-only dependencies and current generated supplies). Set TEMP/TMP to a writable test-artifact directory as in `.artifacts/codex-build.ps1`.

```powershell
ctest --test-dir build/win64-ninja-msvc-debug -j 1 --output-on-failure -E '^apptraverse_window_catalog_win32_close_timing_diagnostic_test$'
ctest --test-dir build/win64-ninja-msvc-debug -j 1 -V -R '^apptraverse_window_catalog_win32_close_timing_diagnostic_test$'
```

No production source changes followed the final accepted build and tests. Only this evidence and progress records were written afterward. Distributed/shared nodes, delivery/caches, upgrades/migrations, platform devices and MinSizeRel remain outside this acceptance scope. History/replay and existing library version matrices are preserved and tested.

## Final executable fingerprints

- `win32_window_catalog_demo.exe`: SHA256 `3d4fb37d8bd019eb83d889943951fbb871c87e48eed2f5a393992a1af45976ed`.
- `win32_window_catalog_demo_load_only.exe`: SHA256 `9e24c2d23d6473160402075dd450d7bdf63017f43bbe197d83567dc2b3f3f2e2`.

## Complete test results

| Test | Result | Seconds |
| --- | --- | ---: |
| `apptraverse_application_evolution_matrix` | PASS | 7.66 |
| `apptraverse_window_schema_layers_roundtrip` | PASS | 5.76 |
| `apptraverse_event_downgrade_matrix` | PASS | 0.35 |
| `apptraverse_event_downgrade_chain_matrix` | PASS | 0.39 |
| `apptraverse_event_version_matrix` | PASS | 6.01 |
| `apptraverse_ui_desktop_window_geometry_test` | PASS | 0.11 |
| `apptraverse_node_local_commit_journal_scaling_test` | PASS | 0.29 |
| `apptraverse_ui_edit_box_headless_test` | PASS | 0.10 |
| `apptraverse_ui_control_materialization_test` | PASS | 1.03 |
| `apptraverse_event_sourced_core_test` | PASS | 0.10 |
| `apptraverse_release_metadata_probe` | PASS | 0.22 |
| `apptraverse_release_metadata_probe_isolated` | PASS | 0.27 |
| `apptraverse_release_metadata_probe_dual` | PASS | 0.24 |
| `apptraverse_release_metadata_config_missing_field` | PASS | 0.03 |
| `apptraverse_release_metadata_config_string_release` | PASS | 0.03 |
| `apptraverse_release_metadata_config_negative_release` | PASS | 0.03 |
| `apptraverse_release_metadata_config_fractional_release` | PASS | 0.04 |
| `apptraverse_release_metadata_config_overflow_release` | PASS | 0.03 |
| `apptraverse_release_metadata_config_missing_file` | PASS | 0.03 |
| `apptraverse_lazy_distilled_embedded_matrix` | PASS | 0.22 |
| `apptraverse_surfaces_distilled_embedded_matrix` | PASS | 0.26 |
| `apptraverse_prefab_active_release_matrix` | PASS | 1.12 |
| `apptraverse_prefab_release_r1_supply_equivalence_test` | PASS | 0.08 |
| `apptraverse_prefab_release_r1_user_equivalence_matrix` | PASS | 0.80 |
| `apptraverse_prefab_dynamic_layer_roundtrip_matrix` | PASS | 1.34 |
| `apptraverse_prefab_late_instance_matrix` | PASS | 1.94 |
| `apptraverse_prefab_compaction_roundtrip_matrix` | PASS | 1.46 |
| `apptraverse_prefab_old_exe_compaction_guard_matrix` | PASS | 0.98 |
| `apptraverse_prefab_release_supply_split_test` | PASS | 0.08 |
| `apptraverse_prefab_release_matrix` | PASS | 1.94 |
| `apptraverse_prefab_release_lifecycle_matrix` | PASS | 0.70 |
| `apptraverse_surfaces_win32_library_consumer_test` | PASS | 0.19 |
| `apptraverse_presenter_load_order_test` | PASS | 0.11 |
| `apptraverse_win32_fatal_ndebug_test` | PASS | 0.13 |
| `apptraverse_win_ui_native_push_button_fatal_test` | PASS | 0.18 |
| `apptraverse_win_ui_native_push_button_test` | PASS | 0.23 |
| `apptraverse_partitioned_model_domain_storage_test` | PASS | 0.11 |
| `apptraverse_model_persistence_runtime_ref_test` | PASS | 0.10 |
| `apptraverse_checkpoint_load_rehydration_test` | PASS | 0.11 |
| `apptraverse_ui_bitmap_publication_test` | PASS | 0.10 |
| `apptraverse_ui_bitmap_structural_publication_test` | PASS | 0.11 |
| `apptraverse_checkpoint_load_cycle_persistence_test` | PASS | 0.09 |
| `apptraverse_presenter_graph_persistence_test` | PASS | 0.11 |
| `apptraverse_presenter_win_hwnd_persistence_test` | PASS | 0.12 |
| `apptraverse_presenter_win_desktop_title_test` | PASS | 0.16 |
| `apptraverse_presenter_win_desktop_frame_sync_test` | PASS | 1.39 |
| `apptraverse_presenter_win_desktop_interactive_drag_e2e_test` | PASS | 0.64 |
| `apptraverse_presenter_win_desktop_frame_publication_e2e_test` | PASS | 0.29 |
| `apptraverse_presenter_platform_persist_roundtrip` | PASS | 0.35 |
| `apptraverse_object_origin_facet_test` | PASS | 0.10 |
| `apptraverse_object_clone_test` | PASS | 1.16 |
| `apptraverse_aether_domain_hierarchy_test` | PASS | 0.09 |
| `apptraverse_object_origin_disk_roundtrip_test` | PASS | 0.10 |
| `apptraverse_object_origin_disk_matrix` | PASS | 0.50 |
| `apptraverse_origin_evolution_matrix` | PASS | 2.17 |
| `apptraverse_model_persistence_test` | PASS | 0.51 |
| `apptraverse_directory_domain_storage_test` | PASS | 0.16 |
| `apptraverse_overlay_domain_storage_test` | PASS | 0.16 |
| `apptraverse_application_contract_headless_test` | PASS | 0.20 |
| `apptraverse_single_file_user_storage_test` | PASS | 0.10 |
| `apptraverse_object_scope_storage_test` | PASS | 0.24 |
| `apptraverse_embedded_distilled_domain_test` | PASS | 0.09 |
| `apptraverse_overlay_user_backing_test` | PASS | 0.09 |
| `apptraverse_historical_base_materialization_test` | PASS | 0.85 |
| `apptraverse_window_catalog_headless_test` | PASS | 12.22 |
| `apptraverse_window_catalog_close_unload_disk_stage_test` | PASS | 1.42 |
| `apptraverse_window_catalog_user_persistence_boundary_test` | PASS | 1.13 |
| `apptraverse_minimal_app_headless_test` | PASS | 20.45 |
| `apptraverse_unload_presented_window_minimal_disk_roundtrip_test` | PASS | 1.51 |
| `apptraverse_unload_presented_window_presenter_branch_disk_roundtrip_test` | PASS | 0.33 |
| `apptraverse_minimal_app_embedded_export_test` | PASS | 0.16 |
| `apptraverse_window_catalog_embedded_overlay_test` | PASS | 4.06 |
| `apptraverse_window_catalog_android_embedded_session_test` | PASS | 1.85 |
| `apptraverse_application_model_runtime_test` | PASS | 0.14 |
| `apptraverse_application_ui_session_test` | PASS | 0.14 |
| `apptraverse_win_application_loading_window_test` | PASS | 0.25 |
| `apptraverse_win_application_host_test` | PASS | 0.20 |
| `apptraverse_window_catalog_model_test` | PASS | 48.35 |
| `apptraverse_window_catalog_win32_acceptance_test` | PASS | 11.06 |
| `apptraverse_window_catalog_win32_chrome_adaptive_test` | PASS | 1.56 |
| `apptraverse_window_catalog_main_native_frame_coalesce_test` | PASS | 1.13 |
| `apptraverse_minimal_app_win32_acceptance_test` | PASS | 1.26 |
| `apptraverse_win_application_message_host_test` | PASS | 0.11 |
| `apptraverse_win_ui_command_routing_test` | PASS | 0.75 |

## Close samples

```text
COLD_ADD_CLOSE rep=0 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@217 list=OK@193 main_exit=OK@61 code=0 OBJ_SYS stdout=0 stderr=0
COLD_ADD_CLOSE rep=1 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@200 list=OK@176 main_exit=OK@46 code=0 OBJ_SYS stdout=0 stderr=0
COLD_ADD_CLOSE rep=2 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@193 list=OK@171 main_exit=OK@47 code=0 OBJ_SYS stdout=0 stderr=0
COLD_ADD_CLOSE rep=3 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@204 list=OK@190 main_exit=OK@61 code=0 OBJ_SYS stdout=0 stderr=0
COLD_ADD_CLOSE rep=4 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@227 list=OK@203 main_exit=OK@46 code=0 OBJ_SYS stdout=0 stderr=0
BURST_CLOSE_IMMEDIATE rep=0 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@209 list=OK@193 main_exit=OK@46 code=0 OBJ_SYS stdout=0 stderr=0
BURST_CLOSE_IMMEDIATE rep=1 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@208 list=OK@189 main_exit=OK@60 code=0 OBJ_SYS stdout=0 stderr=0
BURST_CLOSE_IMMEDIATE rep=2 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@181 list=OK@164 main_exit=OK@46 code=0 OBJ_SYS stdout=0 stderr=0
BURST_CLOSE_IMMEDIATE rep=3 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@238 list=OK@220 main_exit=OK@47 code=0 OBJ_SYS stdout=0 stderr=0
BURST_CLOSE_IMMEDIATE rep=4 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@185 list=OK@185 main_exit=OK@46 code=0 OBJ_SYS stdout=0 stderr=0
BURST_CLOSE_AFTER_SETTLE rep=0 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@156 list=OK@138 main_exit=OK@47 code=0 OBJ_SYS stdout=0 stderr=0
BURST_CLOSE_AFTER_SETTLE rep=1 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@218 list=OK@197 main_exit=OK@47 code=0 OBJ_SYS stdout=0 stderr=0
BURST_CLOSE_AFTER_SETTLE rep=2 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@188 list=OK@176 main_exit=OK@61 code=0 OBJ_SYS stdout=0 stderr=0
BURST_CLOSE_AFTER_SETTLE rep=3 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@216 list=OK@199 main_exit=OK@45 code=0 OBJ_SYS stdout=0 stderr=0
BURST_CLOSE_AFTER_SETTLE rep=4 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@218 list=OK@194 main_exit=OK@47 code=0 OBJ_SYS stdout=0 stderr=0
SAVED_PROFILE_CLOSE rep=0 hwnd_destroy=OK@112 list=OK@115 main_exit=OK@76 code=0 OBJ_SYS stdout=0 stderr=0
SAVED_PROFILE_CLOSE rep=1 hwnd_destroy=OK@116 list=OK@103 main_exit=OK@77 code=0 OBJ_SYS stdout=0 stderr=0
SAVED_PROFILE_CLOSE rep=2 hwnd_destroy=OK@121 list=OK@109 main_exit=OK@78 code=0 OBJ_SYS stdout=0 stderr=0
SAVED_PROFILE_CLOSE rep=3 hwnd_destroy=OK@118 list=OK@107 main_exit=OK@76 code=0 OBJ_SYS stdout=0 stderr=0
SAVED_PROFILE_CLOSE rep=4 hwnd_destroy=OK@122 list=OK@109 main_exit=OK@78 code=0 OBJ_SYS stdout=0 stderr=0
```
