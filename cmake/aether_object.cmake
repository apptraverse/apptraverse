# Full Aether client (registration + adapters + sockets) from the pinned
# aether-client-cpp tree. Replaces the object/domain-only subset so chat can
# SelectClient / RegisterClient while AppTraverse still uses ae::Obj/Domain.

function(_apptraverse_apply_git_patch source_dir patch_file label)
  if(NOT EXISTS "${patch_file}")
    message(FATAL_ERROR "Missing ${label} patch: ${patch_file}")
  endif()
  find_package(Git REQUIRED)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check "${patch_file}"
    WORKING_DIRECTORY "${source_dir}"
    RESULT_VARIABLE _chk
    OUTPUT_VARIABLE _chk_out
    ERROR_VARIABLE _chk_err
  )
  if(NOT _chk EQUAL 0)
    message(FATAL_ERROR
      "${label}: cannot apply ${patch_file} onto ${source_dir} "
      "(rc=${_chk}). ${_chk_out}${_chk_err}")
  endif()
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply "${patch_file}"
    WORKING_DIRECTORY "${source_dir}"
    RESULT_VARIABLE _app
    OUTPUT_VARIABLE _app_out
    ERROR_VARIABLE _app_err
  )
  if(NOT _app EQUAL 0)
    message(FATAL_ERROR
      "${label}: git apply failed for ${patch_file}: ${_app_out}${_app_err}")
  endif()
  message(STATUS "APPTRAVERSE_${label}_PATCH=applied ${patch_file}")
endfunction()

# CPM PATCHES is skipped on SOURCE_CACHE hits. libsodium upstream has no
# CMakeLists.txt; aether-client-cpp's libsodium_cmake.patch supplies it.
function(apptraverse_ensure_libsodium_cmake)
  set(_ae "${aether-client-cpp_SOURCE_DIR}")
  set(_src "")
  if(DEFINED libsodium_SOURCE_DIR AND NOT libsodium_SOURCE_DIR STREQUAL "")
    set(_src "${libsodium_SOURCE_DIR}")
  elseif(DEFINED CPM_PACKAGE_libsodium_SOURCE_DIR)
    set(_src "${CPM_PACKAGE_libsodium_SOURCE_DIR}")
  endif()
  if(_src STREQUAL "")
    message(FATAL_ERROR "libsodium SOURCE_DIR unresolved after CPMAddPackage")
  endif()
  set(_cmake_patch "${_ae}/third_party/libsodium_cmake.patch")
  set(_code_patch "${_ae}/third_party/libsodium.patch")
  if(NOT EXISTS "${_src}/CMakeLists.txt")
    _apptraverse_apply_git_patch("${_src}" "${_cmake_patch}" "LIBSODIUM_CMAKE")
    if(EXISTS "${_code_patch}")
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --check "${_code_patch}"
        WORKING_DIRECTORY "${_src}"
        RESULT_VARIABLE _code_chk
        ERROR_QUIET
      )
      if(_code_chk EQUAL 0)
        _apptraverse_apply_git_patch("${_src}" "${_code_patch}" "LIBSODIUM")
      endif()
    endif()
  else()
    message(STATUS
      "APPTRAVERSE_LIBSODIUM_CMAKE=present source=${_src}")
  endif()
  if(NOT EXISTS "${_src}/CMakeLists.txt")
    message(FATAL_ERROR
      "libsodium still has no CMakeLists.txt after owned patches at ${_src}")
  endif()
  if(NOT TARGET sodium)
    set(_bin "${CMAKE_BINARY_DIR}/_deps/libsodium-build")
    if(DEFINED CPM_PACKAGE_libsodium_BINARY_DIR AND
       NOT CPM_PACKAGE_libsodium_BINARY_DIR STREQUAL "")
      set(_bin "${CPM_PACKAGE_libsodium_BINARY_DIR}")
    endif()
    message(STATUS
      "APPTRAVERSE_LIBSODIUM_ADD_SUBDIR source=${_src} binary=${_bin}")
    add_subdirectory("${_src}" "${_bin}")
  endif()
endfunction()

function(apptraverse_ensure_libbcrypt_cmake)
  set(_ae "${aether-client-cpp_SOURCE_DIR}")
  set(_src "")
  if(DEFINED libbcrypt_SOURCE_DIR AND NOT libbcrypt_SOURCE_DIR STREQUAL "")
    set(_src "${libbcrypt_SOURCE_DIR}")
  elseif(DEFINED CPM_PACKAGE_libbcrypt_SOURCE_DIR)
    set(_src "${CPM_PACKAGE_libbcrypt_SOURCE_DIR}")
  endif()
  if(_src STREQUAL "")
    return()
  endif()
  set(_patch "${_ae}/third_party/libbcrypt.patch")
  if(NOT EXISTS "${_src}/CMakeLists.txt")
    _apptraverse_apply_git_patch("${_src}" "${_patch}" "LIBBCRYPT")
  endif()
  if(NOT TARGET bcrypt AND EXISTS "${_src}/CMakeLists.txt")
    set(_bin "${CMAKE_BINARY_DIR}/_deps/libbcrypt-build")
    if(DEFINED CPM_PACKAGE_libbcrypt_BINARY_DIR AND
       NOT CPM_PACKAGE_libbcrypt_BINARY_DIR STREQUAL "")
      set(_bin "${CPM_PACKAGE_libbcrypt_BINARY_DIR}")
    endif()
    add_subdirectory("${_src}" "${_bin}")
  endif()
endfunction()

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
    DOWNLOAD_ONLY YES
  )
  apptraverse_ensure_libbcrypt_cmake()
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
    DOWNLOAD_ONLY YES
  )
  apptraverse_ensure_libsodium_cmake()
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
  if(NOT libbcrypt_SOURCE_DIR AND DEFINED CPM_PACKAGE_libbcrypt_SOURCE_DIR)
    set(libbcrypt_SOURCE_DIR "${CPM_PACKAGE_libbcrypt_SOURCE_DIR}")
  endif()
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
  )
  if(WIN32)
    target_compile_definitions(aether PUBLIC
      NOMINMAX
      WIN32_LEAN_AND_MEAN
    )
  endif()

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
