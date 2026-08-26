# Object/domain subset of Aether. Does not build the Aether client,
# sockets, crypto, P2P, registration, or cloud stack.

function(apptraverse_add_object_aether)
  set(_src "${aether-client-cpp_SOURCE_DIR}")
  if(_src STREQUAL "")
    message(FATAL_ERROR "aether-client-cpp_SOURCE_DIR is empty after DOWNLOAD_ONLY fetch")
  endif()

  add_library(aether STATIC
    "${_src}/aether/obj/obj.cpp"
    "${_src}/aether/obj/domain.cpp"
    "${_src}/aether/obj/obj_id.cpp"
    "${_src}/aether/obj/registry.cpp"
    "${_src}/aether/obj/obj_ptr_base.cpp"
    "${_src}/aether/ptr/ptr.cpp"
    "${_src}/aether/ptr/ptr_view.cpp"
    "${_src}/aether/ptr/ref_tree.cpp"
    "${_src}/aether/domain_storage/ram_domain_storage.cpp"
  )

  target_include_directories(aether PUBLIC "${_src}")
  target_compile_features(aether PUBLIC cxx_std_20)
  target_link_libraries(aether PUBLIC aether-tele aether::miscpp)
  target_compile_definitions(aether PUBLIC
    AE_DISTILLATION=1
    NOMINMAX
  )

  if(USER_CONFIG AND NOT USER_CONFIG STREQUAL "")
    target_compile_definitions(aether PUBLIC "USER_CONFIG=\"${USER_CONFIG}\"")
  endif()

  if(MSVC)
    target_compile_options(aether PRIVATE /wd4702)
  endif()
endfunction()
