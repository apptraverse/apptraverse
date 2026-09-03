# Full Aether client (registration + adapters + sockets) from the pinned
# aether-client-cpp tree. Replaces the object/domain-only subset so chat can
# SelectClient / RegisterClient while AppTraverse still uses ae::Obj/Domain.

function(apptraverse_add_aether_client_deps)
  set(_ae "${aether-client-cpp_SOURCE_DIR}")
  if(_ae STREQUAL "")
    message(FATAL_ERROR "aether-client-cpp_SOURCE_DIR empty before client deps")
  endif()

  CPMAddPackage(
    NAME libbcrypt
    GIT_REPOSITORY "https://github.com/rg3/libbcrypt.git"
    GIT_TAG "master"
    PATCHES "${_ae}/third_party/libbcrypt.patch"
    OPTIONS "ENABLE_INSTALL OFF"
    EXCLUDE_FROM_ALL FALSE
  )
  CPMAddPackage(
    NAME libhydrogen
    GIT_REPOSITORY "https://github.com/jedisct1/libhydrogen.git"
    GIT_TAG "bbca575"
    PATCHES "${_ae}/third_party/libhydrogen.patch"
    OPTIONS "ENABLE_INSTALL OFF"
    EXCLUDE_FROM_ALL FALSE
  )
  CPMAddPackage(
    NAME libsodium
    GIT_REPOSITORY "https://github.com/jedisct1/libsodium.git"
    GIT_TAG "master"
    PATCHES
      "${_ae}/third_party/libsodium.patch"
      "${_ae}/third_party/libsodium_cmake.patch"
    OPTIONS "ENABLE_INSTALL OFF"
    EXCLUDE_FROM_ALL FALSE
  )
  CPMAddPackage(
    NAME etl
    GIT_REPOSITORY "https://github.com/ETLCPP/etl.git"
    GIT_TAG "20.44.2"
    OPTIONS
      "GIT_DIR_LOOKUP_POLICY ALLOW_LOOKING_ABOVE_CMAKE_SOURCE_DIR"
      "ENABLE_INSTALL OFF"
    PATCHES "${_ae}/third_party/etl.patch"
    EXCLUDE_FROM_ALL FALSE
  )
  CPMAddPackage(
    NAME stdexec
    GIT_REPOSITORY "https://github.com/aethernetio/stdexec.git"
    GIT_TAG "main"
    OPTIONS "STDEXEC_BUILD_EXAMPLES OFF" "STDEXEC_INSTALL OFF"
    EXCLUDE_FROM_ALL FALSE
  )
  CPMAddPackage(
    NAME c-ares
    GIT_REPOSITORY "https://github.com/c-ares/c-ares.git"
    GIT_TAG "main"
    OPTIONS
      "CARES_BUILD_TOOLS OFF"
      "CARES_STATIC ON"
      "CARES_SHARED OFF"
      "CARES_INSTALL OFF"
  )
endfunction()

function(apptraverse_add_full_aether)
  set(_src "${aether-client-cpp_SOURCE_DIR}")
  if(_src STREQUAL "")
    message(FATAL_ERROR "aether-client-cpp_SOURCE_DIR is empty after DOWNLOAD_ONLY fetch")
  endif()

  apptraverse_add_aether_client_deps()

  add_library(aether STATIC)
  set(TARGET_NAME aether)
  add_subdirectory("${_src}/aether" "${CMAKE_BINARY_DIR}/_aether_client_srcs")

  target_include_directories(aether PUBLIC
    $<BUILD_INTERFACE:${_src}>
  )
  if(libbcrypt_SOURCE_DIR)
    target_include_directories(aether PUBLIC
      $<BUILD_INTERFACE:${libbcrypt_SOURCE_DIR}>
    )
  endif()
  target_compile_features(aether PUBLIC cxx_std_20)
  target_link_libraries(aether PUBLIC
    aether-tele
    bcrypt
    sodium
    hydrogen
    gcem
    etl
    stdexec
    ae-numeric
    aether::miscpp
    aether::objects
  )
  target_link_libraries(aether PRIVATE c-ares)

  target_compile_definitions(aether PUBLIC
    AE_DISTILLATION=1
    AE_FILTRATION=1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )

  if(USER_CONFIG AND NOT USER_CONFIG STREQUAL "")
    target_compile_definitions(aether PUBLIC "USER_CONFIG=\"${USER_CONFIG}\"")
  endif()

  if(WIN32)
    target_link_libraries(aether PRIVATE ws2_32)
  endif()

  if(MSVC)
    target_compile_options(aether PRIVATE /wd4702 /wd4996 /Zc:preprocessor)
  endif()
endfunction()

# Back-compat name used by older CMake snippets.
function(apptraverse_add_object_aether)
  apptraverse_add_full_aether()
endfunction()
