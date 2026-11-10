# Domain hierarchy cloning: verification evidence (2026-11-10)

Tested code revision: `61ad37405532e826939ccf60d9af5b443d251654`. The final documentation commit changes no production or test code.

Windows x64 Ninja/MSVC Debug; AE_NO_DEBUG_LOG=ON; /Z7 /Ob0 /Od /RTC1. Assertions and Debug runtime checks are enabled. All test state directories are isolated from user profiles.

82 non-timing tests passed in 151.18 s. The separate Close timing test passed in 30.23 s, with five repetitions of each of four scenarios. Total: 83/83 configured CTest entries passed; no disabled or skipped cases.

Dependency pin: 79283b667cabf689da1059d618fe2a22abf51db4. All 22 repository patches applied to a fresh archive; verified patched source tree: 69698644c506e2c5d4069cb96f8c03b625f5a9b1.

## CTest results

| Test | Result | Seconds |
| --- | --- | ---: |
| apptraverse_application_evolution_matrix | PASS | 7.52248 |
| apptraverse_window_schema_layers_roundtrip | PASS | 5.70819 |
| apptraverse_event_downgrade_matrix | PASS | 0.43574 |
| apptraverse_event_downgrade_chain_matrix | PASS | 0.422077 |
| apptraverse_event_version_matrix | PASS | 5.90586 |
| apptraverse_ui_desktop_window_geometry_test | PASS | 0.0950424 |
| apptraverse_node_local_commit_journal_scaling_test | PASS | 0.275752 |
| apptraverse_ui_edit_box_headless_test | PASS | 0.0987894 |
| apptraverse_ui_control_materialization_test | PASS | 0.880885 |
| apptraverse_event_sourced_core_test | PASS | 0.0929699 |
| apptraverse_release_metadata_probe | PASS | 0.238486 |
| apptraverse_release_metadata_probe_isolated | PASS | 0.265135 |
| apptraverse_release_metadata_probe_dual | PASS | 0.233571 |
| apptraverse_release_metadata_config_missing_field | PASS | 0.0340716 |
| apptraverse_release_metadata_config_string_release | PASS | 0.0281087 |
| apptraverse_release_metadata_config_negative_release | PASS | 0.0307082 |
| apptraverse_release_metadata_config_fractional_release | PASS | 0.0346685 |
| apptraverse_release_metadata_config_overflow_release | PASS | 0.0292797 |
| apptraverse_release_metadata_config_missing_file | PASS | 0.0269526 |
| apptraverse_lazy_distilled_embedded_matrix | PASS | 0.258503 |
| apptraverse_surfaces_distilled_embedded_matrix | PASS | 0.260694 |
| apptraverse_prefab_active_release_matrix | PASS | 1.2423 |
| apptraverse_prefab_release_r1_supply_equivalence_test | PASS | 0.0893547 |
| apptraverse_prefab_release_r1_user_equivalence_matrix | PASS | 0.889955 |
| apptraverse_prefab_dynamic_layer_roundtrip_matrix | PASS | 1.35202 |
| apptraverse_prefab_late_instance_matrix | PASS | 1.96018 |
| apptraverse_prefab_compaction_roundtrip_matrix | PASS | 1.45671 |
| apptraverse_prefab_old_exe_compaction_guard_matrix | PASS | 1.00408 |
| apptraverse_prefab_release_supply_split_test | PASS | 0.105908 |
| apptraverse_prefab_release_matrix | PASS | 2.16041 |
| apptraverse_prefab_release_lifecycle_matrix | PASS | 0.756239 |
| apptraverse_surfaces_win32_library_consumer_test | PASS | 0.198571 |
| apptraverse_presenter_load_order_test | PASS | 0.101983 |
| apptraverse_win32_fatal_ndebug_test | PASS | 0.130299 |
| apptraverse_win_ui_native_push_button_fatal_test | PASS | 0.187122 |
| apptraverse_win_ui_native_push_button_test | PASS | 0.211476 |
| apptraverse_partitioned_model_domain_storage_test | PASS | 0.0872634 |
| apptraverse_model_persistence_runtime_ref_test | PASS | 0.0974504 |
| apptraverse_checkpoint_load_rehydration_test | PASS | 0.113088 |
| apptraverse_ui_bitmap_publication_test | PASS | 0.0751908 |
| apptraverse_ui_bitmap_structural_publication_test | PASS | 0.104087 |
| apptraverse_checkpoint_load_cycle_persistence_test | PASS | 0.0958526 |
| apptraverse_presenter_graph_persistence_test | PASS | 0.100851 |
| apptraverse_presenter_win_hwnd_persistence_test | PASS | 0.12976 |
| apptraverse_presenter_win_desktop_title_test | PASS | 0.155738 |
| apptraverse_presenter_win_desktop_frame_sync_test | PASS | 1.07786 |
| apptraverse_presenter_win_desktop_interactive_drag_e2e_test | PASS | 0.496171 |
| apptraverse_presenter_win_desktop_frame_publication_e2e_test | PASS | 0.244374 |
| apptraverse_presenter_platform_persist_roundtrip | PASS | 0.338026 |
| apptraverse_object_origin_facet_test | PASS | 0.107448 |
| apptraverse_object_clone_test | PASS | 1.25651 |
| apptraverse_aether_domain_hierarchy_test | PASS | 0.100748 |
| apptraverse_object_origin_disk_roundtrip_test | PASS | 0.112185 |
| apptraverse_object_origin_disk_matrix | PASS | 0.429956 |
| apptraverse_origin_evolution_matrix | PASS | 2.04773 |
| apptraverse_model_persistence_test | PASS | 0.527254 |
| apptraverse_directory_domain_storage_test | PASS | 0.157826 |
| apptraverse_overlay_domain_storage_test | PASS | 0.149627 |
| apptraverse_application_contract_headless_test | PASS | 0.189967 |
| apptraverse_embedded_distilled_domain_test | PASS | 0.0926652 |
| apptraverse_overlay_user_backing_test | PASS | 0.108127 |
| apptraverse_historical_base_materialization_test | PASS | 0.89602 |
| apptraverse_window_catalog_headless_test | PASS | 4.14217 |
| apptraverse_window_catalog_close_unload_disk_stage_test | PASS | 1.81042 |
| apptraverse_window_catalog_user_persistence_boundary_test | PASS | 4.42227 |
| apptraverse_minimal_app_headless_test | PASS | 20.2938 |
| apptraverse_unload_presented_window_minimal_disk_roundtrip_test | PASS | 1.54877 |
| apptraverse_unload_presented_window_presenter_branch_disk_roundtrip_test | PASS | 0.325018 |
| apptraverse_minimal_app_embedded_export_test | PASS | 0.14708 |
| apptraverse_window_catalog_embedded_overlay_test | PASS | 2.63623 |
| apptraverse_window_catalog_android_embedded_session_test | PASS | 2.05406 |
| apptraverse_application_model_runtime_test | PASS | 0.107592 |
| apptraverse_application_ui_session_test | PASS | 0.156009 |
| apptraverse_win_application_loading_window_test | PASS | 0.255809 |
| apptraverse_win_application_host_test | PASS | 0.179967 |
| apptraverse_window_catalog_model_test | PASS | 43.0835 |
| apptraverse_window_catalog_win32_acceptance_test | PASS | 19.1427 |
| apptraverse_window_catalog_win32_chrome_adaptive_test | PASS | 1.37066 |
| apptraverse_window_catalog_main_native_frame_coalesce_test | PASS | 0.893712 |
| apptraverse_minimal_app_win32_acceptance_test | PASS | 3.6533 |
| apptraverse_win_application_message_host_test | PASS | 0.115473 |
| apptraverse_win_ui_command_routing_test | PASS | 0.711443 |
| apptraverse_window_catalog_win32_close_timing_diagnostic_test | PASS | 30.229 |

## Close timing

| Scenario | Repeats | HWND destroy median, ms | List closed median, ms |
| --- | ---: | ---: | ---: |
| COLD_ADD_CLOSE | 5 | 679 | 663 |
| BURST_CLOSE_IMMEDIATE | 5 | 830 | 814 |
| BURST_CLOSE_AFTER_SETTLE | 5 | 842 | 828 |
| SAVED_PROFILE_CLOSE | 5 | 225 | 208 |

All 20 observations reported HWND destruction, list closure, and process exit successfully. Main exit ranged from 108 to 157 ms; every process exited with code 0. OBJ_SYS stdout/stderr counts were zero. The saved seed was runtime_prepared because the optional bundled seed was missing. Hidden-window timing was unavailable (NA); stable geometry in AFTER_SETTLE does not prove the publication queue was drained.

Raw per-repetition output:

```text
79: COLD_ADD_CLOSE rep=0 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@667 list=OK@648 main_exit=OK@123 code=0 OBJ_SYS stdout=0 stderr=0
79: COLD_ADD_CLOSE rep=1 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@691 list=OK@674 main_exit=OK@108 code=0 OBJ_SYS stdout=0 stderr=0
79: COLD_ADD_CLOSE rep=2 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@679 list=OK@663 main_exit=OK@109 code=0 OBJ_SYS stdout=0 stderr=0
79: COLD_ADD_CLOSE rep=3 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@655 list=OK@645 main_exit=OK@124 code=0 OBJ_SYS stdout=0 stderr=0
79: COLD_ADD_CLOSE rep=4 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@696 list=OK@680 main_exit=OK@122 code=0 OBJ_SYS stdout=0 stderr=0
79: BURST_CLOSE_IMMEDIATE rep=0 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@854 list=OK@844 main_exit=OK@157 code=0 OBJ_SYS stdout=0 stderr=0
79: BURST_CLOSE_IMMEDIATE rep=1 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@772 list=OK@754 main_exit=OK@139 code=0 OBJ_SYS stdout=0 stderr=0
79: BURST_CLOSE_IMMEDIATE rep=2 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@895 list=OK@869 main_exit=OK@156 code=0 OBJ_SYS stdout=0 stderr=0
79: BURST_CLOSE_IMMEDIATE rep=3 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@830 list=OK@807 main_exit=OK@155 code=0 OBJ_SYS stdout=0 stderr=0
79: BURST_CLOSE_IMMEDIATE rep=4 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@826 list=OK@814 main_exit=OK@139 code=0 OBJ_SYS stdout=0 stderr=0
79: BURST_CLOSE_AFTER_SETTLE rep=0 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@842 list=OK@817 main_exit=OK@140 code=0 OBJ_SYS stdout=0 stderr=0
79: BURST_CLOSE_AFTER_SETTLE rep=1 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@870 list=OK@856 main_exit=OK@155 code=0 OBJ_SYS stdout=0 stderr=0
79: BURST_CLOSE_AFTER_SETTLE rep=2 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@854 list=OK@841 main_exit=OK@139 code=0 OBJ_SYS stdout=0 stderr=0
79: BURST_CLOSE_AFTER_SETTLE rep=3 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@842 list=OK@828 main_exit=OK@141 code=0 OBJ_SYS stdout=0 stderr=0
79: BURST_CLOSE_AFTER_SETTLE rep=4 wm_close=OK hwnd_hidden=NA@-1 hwnd_destroy=OK@830 list=OK@813 main_exit=OK@139 code=0 OBJ_SYS stdout=0 stderr=0
79: SAVED_PROFILE_CLOSE rep=0 hwnd_destroy=OK@238 list=OK@214 main_exit=OK@140 code=0 OBJ_SYS stdout=0 stderr=0
79: SAVED_PROFILE_CLOSE rep=1 hwnd_destroy=OK@225 list=OK@206 main_exit=OK@123 code=0 OBJ_SYS stdout=0 stderr=0
79: SAVED_PROFILE_CLOSE rep=2 hwnd_destroy=OK@220 list=OK@208 main_exit=OK@155 code=0 OBJ_SYS stdout=0 stderr=0
79: SAVED_PROFILE_CLOSE rep=3 hwnd_destroy=OK@221 list=OK@205 main_exit=OK@141 code=0 OBJ_SYS stdout=0 stderr=0
79: SAVED_PROFILE_CLOSE rep=4 hwnd_destroy=OK@227 list=OK@214 main_exit=OK@156 code=0 OBJ_SYS stdout=0 stderr=0
```

## Persistence boundary and model-only diagnostic

```text
CLOSE_STAGE MODEL_COLD model_close_ms=573 store_calls=452 store_disk_bytes=0 store_accepted_bytes=4336 publication_bytes=0
CLOSE_STAGE MODEL_BURST model_close_ms=879 store_calls=638 store_disk_bytes=0 store_accepted_bytes=7002 publication_bytes=0
window_catalog_close_unload_disk_stage_test OK
WINDOW_CATALOG_USER_PERSISTENCE_BOUNDARY ok boot_files=0 after_close=220 after_shutdown=247
ALL_USER_PERSISTENCE_SCENARIOS_OK
```

The boundary test asserts zero USER Store calls during runtime work, including bootstrap, Add, geometry and language changes, then verifies cold disk restoration before shutdown. It also tests seeded USER and two independent sessions. Model-only diagnostic times are single samples, not comparable medians. Its physical-byte counter reports zero through the overlay and must not be interpreted as absence of disk writes; cold disk verification and directory counts establish persistence. No CPU sampling or separate Save/reference-release attribution was collected.

## Executable identities

| Executable | SHA-256 |
| --- | --- |
| win32_window_catalog_demo.exe | d0d331fb1cdf4d27fdac0d9c6d78368cc052fa2bc6b2531c7ab2b5f96e9f21a8 |
| apptraverse_window_catalog_win32_close_timing_diagnostic_test.exe | b63096f1fc1eeda9b92e4bbec9db48beb89da1ec81688ec9359a0c7d50a2b68e |

Local full artifacts: .artifacts/acceptance-complete.log and .xml; .artifacts/close-timing.log; build/win64-ninja-msvc-debug/.artifacts/close-timing.xml; .artifacts/patch-chain-verification/20261110T084449671798Z/. They are generated evidence, not dependency cache modifications.
