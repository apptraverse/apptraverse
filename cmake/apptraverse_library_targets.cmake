# AppTraverse library layering (Phase 1 Windows).
# apptraverse — aggregate INTERFACE for existing consumers.
# apptraverse_core — storage, serialization, Node/Event (no Surface / no app root).
# apptraverse_ui — control models, neutral presenters, localization.
# apptraverse_application_runtime — model/UI mirror, proxy, publication session.
# apptraverse_shared_sync — optional SharedNode / sync (no aether client required to link core+ui+runtime).

set(_apptraverse_include ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(apptraverse_core STATIC
  src/apptraverse.cpp
  src/noninteractive_crt.cpp
  src/directory_domain_storage.cpp
  src/overlay_domain_storage.cpp
  src/partitioned_model_domain_storage.cpp
  src/model_persistence.cpp
  src/object_serialization.cpp
  src/publication_serialization_scope.cpp
  src/link.cpp
  src/graph_mirror.cpp
  src/memory_transport.cpp
  src/sync_frame.cpp
)
target_include_directories(apptraverse_core PUBLIC ${_apptraverse_include})
target_link_libraries(apptraverse_core PUBLIC
  aether::objects
  aether::miscpp
  apptraverse_compile_policy
)
target_compile_definitions(apptraverse_core PUBLIC
  AE_DISTILLATION=1
  AE_FILTRATION=1
)
target_compile_features(apptraverse_core PUBLIC cxx_std_20)

add_library(apptraverse_ui STATIC
  src/ui/ui_localization_string.cpp
  src/ui/ui_locale_settings.cpp
  src/ui/ui_window.cpp
  src/ui/ui_desktop_window.cpp
  src/ui/ui_edit_box.cpp
  src/ui/ui_push_button.cpp
  src/ui/ui_list_container.cpp
  src/ui/ui_bitmap_image.cpp
  src/ui/ui_presenters.cpp
  src/ui/ui_registration.cpp
)
target_link_libraries(apptraverse_ui PUBLIC apptraverse_core)
target_include_directories(apptraverse_ui PUBLIC ${_apptraverse_include})
target_compile_features(apptraverse_ui PUBLIC cxx_std_20)

add_library(apptraverse_application_runtime STATIC
  src/runtime_lifecycle.cpp
  src/model_object_proxy.cpp
  src/presentation_size_queue.cpp
  src/application/model_publication_session.cpp
  src/ui_mirror.cpp
  src/model_runtime.cpp
)
target_link_libraries(apptraverse_application_runtime PUBLIC
  apptraverse_ui
  apptraverse_shared_sync
)
target_include_directories(apptraverse_application_runtime PUBLIC ${_apptraverse_include})
target_compile_features(apptraverse_application_runtime PUBLIC cxx_std_20)

add_library(apptraverse_shared_sync STATIC
  src/shared_node.cpp
  src/shared_network_graph.cpp
  src/shared_sync_runtime.cpp
)
target_link_libraries(apptraverse_shared_sync PUBLIC apptraverse_core)
target_include_directories(apptraverse_shared_sync PUBLIC ${_apptraverse_include})
target_compile_features(apptraverse_shared_sync PUBLIC cxx_std_20)

add_library(apptraverse INTERFACE)
target_link_libraries(apptraverse INTERFACE apptraverse_application_runtime)

if(WIN32)
  target_compile_definitions(apptraverse_core PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
endif()
