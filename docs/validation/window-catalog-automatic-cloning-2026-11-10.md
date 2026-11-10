# Automatic clone boundaries and final Windows acceptance — 2026-11-10

Implementation and acceptance are complete on `feature/domain-hierarchy-cloning`, following baseline `166631a95bbdfda1c53c0b72778e073b7470fa11`. Adaptive WIP was not modified. This record covers the final source, including the scoped serialized-pointer visitor.

## Clone contract

- The explicitly selected root is copied, including when that root is a shared constant.
- Ordinary references to mutable objects in the source domain/subgraph are copied recursively. Backlinks and cycles bind to the corresponding copies.
- References resolved in an enclosing domain, or marked as immutable shared objects, retain their identities and terminate traversal.
- Persistent object-scope metadata preserves the local/enclosing distinction when a graph is stored in a flattened object-ID index and loaded in a new process. Nested scopes retain their nesting with remapped scope-root IDs.
- Missing references are failures, not automatically accepted shared boundaries.
- A stored local object shadows an already-loaded ancestor object with the same ID. Clone snapshots use that local identity, and staging selects factories from the actual snapshot layers.

`CloneOptions::boundary_ids` remains available for compatibility with existing library callers and fixtures. Window Catalog no longer supplies an application-maintained boundary-ID list. `stable_prefab_ids` is a separate origin-provenance declaration and must not be described as automatic copy-boundary metadata.

## Representation and lifecycle

`ae::ObjectScope` records the scope root, its parent scope root, and the semantic constant flag. The metadata belongs to the common object/storage layer; application S0 field layouts and ObjPtr wire bytes are unchanged. Directory storage writes per-object `.scope` metadata at explicit persistence boundaries. Embedded supplies carry generated scope records. RAM scratch storage retains the same metadata while cloning or capturing history.

A read-only template in BACKING is not necessarily a shared constant: its presenter and other internal mutable-instance components must be copied. Shared labels and other true constants remain shared. The implementation does not infer this distinction from hardcoded class names or ObjIds.

Cloning still uses operation-local scratch, staging objects, a child domain, and final reference rebinding. The result is retained through the existing Ptr/ObjectLink ownership mechanism. There is no process-global keepalive or mutable serialized USER session database. RAM buffers are transient operation inputs; they are not a persistence flush cache.

The snapshot resolves live-versus-stored identity once. Factory selection and copied scope metadata come from that snapshot, avoiding a later ancestor lookup that could substitute another object with the same ID. Both live and explicit disk-persist clone paths carry the same scope metadata. New clone scopes are rebased to the new root; historical bases inherit their owner's scope.

Construction scope remains meaningful for later mutations: newly attached objects must be created in their intended subgraph scope. The implementation does not guess semantic ownership from reference counts or temporary keepalive handles.

## Difference from the Cursor handoff

The Cursor handoff already contained domain hierarchy and child-domain clone finalization. It still relied on application-supplied boundary IDs, and its incomplete persistence/history paths required subsequent correctness work.

The earlier completed branch fixed pointer ownership, cold presenter persistence, and runtime USER write boundaries. This iteration adds the automatic domain/constant clone rule, generic persistent scope metadata, and cold local-storage shadowing. It does not replace Ptr with raw pointers, introduce a new application checkpoint format, or claim that temporary domains alone discover a copy set from an unannotated flat graph.

## Additional defects exposed during acceptance

The full title fixture exposed a stale duplicate catalog: the clone's temporary storage was an overlay that advertised parent BACKING layers as local layers. After correct local-storage precedence was introduced, those layers caused a second catalog object to be constructed instead of resolving the canonical live parent. Both clone staging and final child domains now expose only their own scratch layers and resolve external objects through their parent domains. A regression uses a parent whose stored value is 10 and live value is 20, with a custom-serialized parent link omitted from reflection; the clone must retain the exact parent pointer and value 20. The clone regression and all phases of the unchanged title fixture pass in the final run.

A further focused regression then demonstrated that a lazy raw ObjPtr field omitted from reflection retained the destroyed temporary domain. The final implementation adds an opt-in, per-DomainGraph serialized-reference visitor used only for clone finalization. This is not a process-global callback. It relocates raw serialized fields even when reflection omits them; the existing ObjectLink path remains in place. All copies are registered in the target before this synchronous callback runs.

The reference visitor preserves flags exactly. Copied lazy children acquire owning cached references because their runtime layers have not been persisted. External lazy references retain an empty cache and target-domain identity, even when the external object is already live. The callback never calls Load or writes storage. Dedicated tests cover cold external references, zero external Load/Store during cloning, retained unreflected lazy children after source/result/temporary-domain destruction, and release of those children with the sole owning root. These cases pass in both Debug and MinSizeRel.

## Final verification

Windows x64, MSVC 14.50, Ninja. Debug uses `/Z7 /Ob0 /Od /RTC1`, assertions enabled and `AE_NO_DEBUG_LOG=ON`. MinSizeRel uses the existing optimized configuration. No build ran concurrently with the final timing run.

| Check | Final result |
| --- | --- |
| Fresh pinned dependency source plus repository patch chain | 26/26 PASS |
| Debug CTest, excluding the separate timing entry | 83/83 PASS, no skips, 193.25 s |
| Debug Close timing entry | PASS, 20/20 observations, 28.59 s |
| Total configured Debug CTest entries | 84/84 PASS |
| MinSizeRel clone and scope/storage tests | 2/2 PASS |
| MinSizeRel cold persistence boundary and Win32 acceptance | 2/2 PASS, 22.39 s |
| Normal Debug fresh graph on repeated launch | PASS in Win32 acceptance |
| Disk write failure, private presenter state, ancestor presenter storage | PASS |

The final clone tests explicitly cover a serialized raw pointer omitted from reflection, both loaded and lazy parent references, a cold lazy parent, and a lazy internal copied child after temporary domains and result keepalives are gone. External lazy references cause no parent Load or Store during cloning. The full WindowCatalog title-contract test is unchanged and passes after the structural parent-identity fix.

Fresh dependency evidence: `.artifacts/patch-chain-verification/20261110T180342637249Z/`. Pin: `79283b667cabf689da1059d618fe2a22abf51db4`; patched tree: `ca84942a29bfd1bf72fc5b95a5aaea3ff6d92a9a`. All patches were applied to a fresh isolated archive; existing CPM source caches were not patched manually.

Debug-prepared and MinSizeRel-consumed embedded header SHA-256: `7c37276296592fce6bbdbb0182741bc02b7778e4aa793306a097e20fba6b684c`. The MinSizeRel normal demo is **508,928 bytes**.

## Debug preparation and persistence

Normal `win32_window_catalog_demo.exe` in Debug prepares the initial graph and starts with a new `debug-run-<id>/user` directory on every launch. It never loads the previous USER. Old profiles are left intact. The same Debug executable supports `--distill-only`; the separate Windows catalog exporter executable/source was removed. The named load-only test consumer remains available for same-supply restart and timing tests.

MinSizeRel consumes the Debug-prepared embedded header and restores its selected USER profile. On disk, object/class/version files remain separate; `.scope` is common per-object membership metadata, not an application checkpoint or a pack file. The C++ header is the compiled representation of BACKING.

Runtime bootstrap consumption, Add, resize and language changes perform zero USER Store calls in boundary tests. Saves occur at subgraph unload and application shutdown. Transient serialization buffers for cloning/history and UI mirror publications remain in RAM; there is no serialized USER session store or flush cache. `ae::Ptr`/ProxyPtr ownership remains in use.

## Close performance

The controlled optimization A/B used one fixed supply and five repetitions per case. It established the directory-creation and repeated-save costs independently of later scope integration. The final column is a separately measured final supply; it is not claimed as another fixed-supply A/B pair.

| Scenario, HWND destruction median (ms) | Fixed-supply before | Fixed-supply optimized | Final source/supply |
| --- | ---: | ---: | ---: |
| Cold Add + Close | 696 | 416 | 400 |
| Burst, immediate Close | 859 | 537 | 505 |
| Burst, stable HWND geometry | 1082 | 566 | 550 |
| Saved profile | 231 | 165 | 171 |

All final closes and main exits succeeded. Main exit ranged from 125 to 190 ms, with no timeout or OBJ_SYS output. The saved seed was prepared at runtime from the same executable/header. Stable HWND geometry does not prove that publication queues have drained; window hiding is not separately measured.

Final model-only cold/burst Close took 316/523 ms; the same RAM control took 4/7 ms. Cold/burst disk commits were 173/266 class layers plus 21 scope records (273 metadata bytes) each. The remaining cost is many individual filesystem operations. The changes avoid recursive directory preparation when the parent exists and save window/presenter with one traversal while preserving the private presenter state and lazy persisted link. Different-domain presenters retain their own storage owner.

See [controlled Close investigation](window-catalog-debug-close-2026-11-10.md) and [Debug preparation workflow](window-catalog-debug-preparation-2026-11-10.md).

## Reproduction and evidence

Use the native patch verifier and build commands in [the plan/progress document](../domain-hierarchy-cloning-plan-progress.md). Set both `CPM_aether-objects_SOURCE` and `FETCHCONTENT_SOURCE_DIR_AETHER-OBJECTS` to the fresh verified tree. Build the Debug supply before configuring/building its MinSizeRel consumer.

Final logs retained locally:

- `.artifacts/verify-final-26-patches.log`
- `.artifacts/final26-clone-build.log`, `final26-clone-test.log`
- `.artifacts/final26-debug-build.log`, `final26-debug-tests.log`
- `.artifacts/final26-minsizerel-core-build.log`, `final26-minsizerel-core-test.log`
- `.artifacts/final26-minsizerel-app-build.log`, `final26-minsizerel-app-tests.log`
- `.artifacts/final26-close-timing.log`, `final26-close-stages.log`

<details>
<summary>Final Debug tests, excluding the separately reported timing entry</summary>

| Test | PASS duration (s) |
| --- | ---: |
| `apptraverse_application_evolution_matrix` | 7.87 |
| `apptraverse_window_schema_layers_roundtrip` | 7.42 |
| `apptraverse_event_downgrade_matrix` | 0.59 |
| `apptraverse_event_downgrade_chain_matrix` | 0.55 |
| `apptraverse_event_version_matrix` | 9.61 |
| `apptraverse_ui_desktop_window_geometry_test` | 0.08 |
| `apptraverse_node_local_commit_journal_scaling_test` | 0.29 |
| `apptraverse_ui_edit_box_headless_test` | 0.08 |
| `apptraverse_ui_control_materialization_test` | 1.15 |
| `apptraverse_event_sourced_core_test` | 0.07 |
| `apptraverse_release_metadata_probe` | 0.36 |
| `apptraverse_release_metadata_probe_isolated` | 0.31 |
| `apptraverse_release_metadata_probe_dual` | 0.32 |
| `apptraverse_release_metadata_config_missing_field` | 0.06 |
| `apptraverse_release_metadata_config_string_release` | 0.05 |
| `apptraverse_release_metadata_config_negative_release` | 0.05 |
| `apptraverse_release_metadata_config_fractional_release` | 0.05 |
| `apptraverse_release_metadata_config_overflow_release` | 0.05 |
| `apptraverse_release_metadata_config_missing_file` | 0.04 |
| `apptraverse_lazy_distilled_embedded_matrix` | 0.35 |
| `apptraverse_surfaces_distilled_embedded_matrix` | 0.36 |
| `apptraverse_prefab_active_release_matrix` | 1.46 |
| `apptraverse_prefab_release_r1_supply_equivalence_test` | 0.08 |
| `apptraverse_prefab_release_r1_user_equivalence_matrix` | 1.13 |
| `apptraverse_prefab_dynamic_layer_roundtrip_matrix` | 2.03 |
| `apptraverse_prefab_late_instance_matrix` | 3.20 |
| `apptraverse_prefab_compaction_roundtrip_matrix` | 2.03 |
| `apptraverse_prefab_old_exe_compaction_guard_matrix` | 1.33 |
| `apptraverse_prefab_release_supply_split_test` | 0.08 |
| `apptraverse_prefab_release_matrix` | 3.12 |
| `apptraverse_prefab_release_lifecycle_matrix` | 1.43 |
| `apptraverse_surfaces_win32_library_consumer_test` | 0.51 |
| `apptraverse_presenter_load_order_test` | 0.32 |
| `apptraverse_win32_fatal_ndebug_test` | 0.63 |
| `apptraverse_win_ui_native_push_button_fatal_test` | 0.99 |
| `apptraverse_win_ui_native_push_button_test` | 0.41 |
| `apptraverse_partitioned_model_domain_storage_test` | 0.08 |
| `apptraverse_model_persistence_runtime_ref_test` | 0.08 |
| `apptraverse_checkpoint_load_rehydration_test` | 0.09 |
| `apptraverse_ui_bitmap_publication_test` | 0.09 |
| `apptraverse_ui_bitmap_structural_publication_test` | 0.08 |
| `apptraverse_checkpoint_load_cycle_persistence_test` | 0.09 |
| `apptraverse_presenter_graph_persistence_test` | 0.10 |
| `apptraverse_presenter_win_hwnd_persistence_test` | 0.12 |
| `apptraverse_presenter_win_desktop_title_test` | 0.16 |
| `apptraverse_presenter_win_desktop_frame_sync_test` | 1.56 |
| `apptraverse_presenter_win_desktop_interactive_drag_e2e_test` | 0.58 |
| `apptraverse_presenter_win_desktop_frame_publication_e2e_test` | 0.28 |
| `apptraverse_presenter_platform_persist_roundtrip` | 0.30 |
| `apptraverse_object_origin_facet_test` | 0.07 |
| `apptraverse_object_clone_test` | 3.68 |
| `apptraverse_aether_domain_hierarchy_test` | 0.11 |
| `apptraverse_object_origin_disk_roundtrip_test` | 0.15 |
| `apptraverse_object_origin_disk_matrix` | 0.54 |
| `apptraverse_origin_evolution_matrix` | 2.35 |
| `apptraverse_model_persistence_test` | 0.50 |
| `apptraverse_directory_domain_storage_test` | 0.14 |
| `apptraverse_overlay_domain_storage_test` | 0.12 |
| `apptraverse_application_contract_headless_test` | 0.18 |
| `apptraverse_object_scope_storage_test` | 0.23 |
| `apptraverse_embedded_distilled_domain_test` | 0.10 |
| `apptraverse_overlay_user_backing_test` | 0.10 |
| `apptraverse_historical_base_materialization_test` | 0.92 |
| `apptraverse_window_catalog_headless_test` | 4.46 |
| `apptraverse_window_catalog_close_unload_disk_stage_test` | 1.47 |
| `apptraverse_window_catalog_user_persistence_boundary_test` | 7.97 |
| `apptraverse_minimal_app_headless_test` | 23.52 |
| `apptraverse_unload_presented_window_minimal_disk_roundtrip_test` | 1.38 |
| `apptraverse_unload_presented_window_presenter_branch_disk_roundtrip_test` | 0.33 |
| `apptraverse_minimal_app_embedded_export_test` | 0.17 |
| `apptraverse_window_catalog_embedded_overlay_test` | 2.41 |
| `apptraverse_window_catalog_android_embedded_session_test` | 1.77 |
| `apptraverse_application_model_runtime_test` | 0.19 |
| `apptraverse_application_ui_session_test` | 0.14 |
| `apptraverse_win_application_loading_window_test` | 0.25 |
| `apptraverse_win_application_host_test` | 0.19 |
| `apptraverse_window_catalog_model_test` | 51.93 |
| `apptraverse_window_catalog_win32_acceptance_test` | 26.35 |
| `apptraverse_window_catalog_win32_chrome_adaptive_test` | 1.25 |
| `apptraverse_window_catalog_main_native_frame_coalesce_test` | 0.74 |
| `apptraverse_minimal_app_win32_acceptance_test` | 3.71 |
| `apptraverse_win_application_message_host_test` | 0.10 |
| `apptraverse_win_ui_command_routing_test` | 0.71 |

</details>

## Compatibility boundaries

No migration of old experimental application profiles is claimed. Class S0 wire compatibility does not by itself establish compatibility of a profile lacking the new generic scope metadata or a profile produced by a different nondeterministic supply build. Those questions must be reported separately from same-build save/restart acceptance.

This acceptance is Windows-only. Native Android/Linux/macOS/iOS/Web execution, distributed delivery/resource caches and general production hardening remain deferred. The shared Windows-hosted Android embedded-session fixture passes, but is not a device run. The Debug-preparation CMake path was verified with single-config Ninja, not a multi-config generator. Deterministic byte-for-byte regeneration across independent distill runs is not claimed.
