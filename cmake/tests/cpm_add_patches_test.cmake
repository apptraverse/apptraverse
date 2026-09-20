# cmake/tests/cpm_add_patches_test.cmake
#
# Exercises the real cpm_add_patches() from cmake/CPM.cmake (not a rewrite).
# Dependency SOURCE_CACHE is irrelevant: patches are applied onto disposable
# fixture trees via the generated PATCH_COMMAND list.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED APPTRAVERSE_SOURCE_DIR)
  get_filename_component(APPTRAVERSE_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

set(_cpm "${APPTRAVERSE_SOURCE_DIR}/cmake/CPM.cmake")
if(NOT EXISTS "${_cpm}")
  message(FATAL_ERROR "cpm_add_patches_test: missing ${_cpm}")
endif()
include("${_cpm}")

if(NOT COMMAND cpm_add_patches)
  message(FATAL_ERROR "cpm_add_patches_test: cpm_add_patches not defined")
endif()

# In `cmake -P` script mode CMAKE_CURRENT_BINARY_DIR may be empty (or the
# caller's cwd). Prefer an explicit -DFIXTURE_ROOT=... when provided.
if(DEFINED FIXTURE_ROOT AND NOT FIXTURE_ROOT STREQUAL "")
  set(_fixture_root "${FIXTURE_ROOT}/cpm_patch_fixtures")
elseif(NOT CMAKE_CURRENT_BINARY_DIR STREQUAL "")
  set(_fixture_root "${CMAKE_CURRENT_BINARY_DIR}/cpm_patch_fixtures")
else()
  set(_fixture_root "/tmp/cpm_patch_fixtures")
endif()
file(REMOVE_RECURSE "${_fixture_root}")
file(MAKE_DIRECTORY "${_fixture_root}")

set(_patches_dir "${_fixture_root}/patches")
file(MAKE_DIRECTORY "${_patches_dir}")

# --- fixture patches -------------------------------------------------------
file(WRITE "${_patches_dir}/01_hello.patch"
"--- a/hello.txt
+++ b/hello.txt
@@ -1 +1 @@
-hello
+hello-patched
")

file(WRITE "${_patches_dir}/02_extra.patch"
"--- a/extra.txt
+++ b/extra.txt
@@ -1 +1 @@
-extra
+extra-patched
")

file(WRITE "${_patches_dir}/bad_garbage.patch" "this is not a patch file\n")

file(WRITE "${_patches_dir}/incompatible.patch"
"--- a/hello.txt
+++ b/hello.txt
@@ -1 +1 @@
-completely-different-context
+nope
")

# Partial: first hunk matches hello.txt; second modifies a missing file with
# non-create context so `patch` rejects a hunk (create-file @@ -0,0 would
# succeed on GNU patch and is not a partial-failure case).
file(WRITE "${_patches_dir}/partial.patch"
"--- a/hello.txt
+++ b/hello.txt
@@ -1 +1 @@
-hello
+hello-partial
--- a/missing.txt
+++ b/missing.txt
@@ -1 +1 @@
-exists
+changed
")

function(apptraverse_seed_tree dest)
  file(REMOVE_RECURSE "${dest}")
  file(MAKE_DIRECTORY "${dest}")
  file(WRITE "${dest}/hello.txt" "hello\n")
  file(WRITE "${dest}/extra.txt" "extra\n")
endfunction()

function(apptraverse_read_file path out_var)
  file(READ "${path}" _text)
  set(${out_var} "${_text}" PARENT_SCOPE)
endfunction()

# Run the PATCH_COMMAND list produced by cpm_add_patches on workdir.
# Substitutes <SOURCE_DIR> the same way ExternalProject does.
function(apptraverse_run_patch_command_list workdir expect_success)
  set(_args ${CPM_ARGS_UNPARSED_ARGUMENTS})
  list(LENGTH _args _n)
  if(_n LESS 2)
    message(FATAL_ERROR "PATCH_COMMAND list too short: ${_args}")
  endif()
  list(GET _args 0 _head)
  if(NOT _head STREQUAL "PATCH_COMMAND")
    message(FATAL_ERROR "expected PATCH_COMMAND, got ${_head}")
  endif()
  list(REMOVE_AT _args 0)

  set(_cmd "")
  set(_saw_failure FALSE)
  while(_args)
    list(GET _args 0 _tok)
    list(REMOVE_AT _args 0)
    if(_tok STREQUAL "&&")
      set(_fixed "")
      foreach(_t IN LISTS _cmd)
        string(REPLACE "<SOURCE_DIR>" "${workdir}" _t "${_t}")
        list(APPEND _fixed "${_t}")
      endforeach()
      execute_process(
        COMMAND ${_fixed}
        WORKING_DIRECTORY "${workdir}"
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)
      if(NOT _rc EQUAL 0)
        set(_saw_failure TRUE)
        if(expect_success)
          message(FATAL_ERROR
            "generated patch command failed rc=${_rc}\n${_out}${_err}\n"
            "cmd=${_fixed}")
        endif()
        break()
      endif()
      set(_cmd "")
    else()
      list(APPEND _cmd "${_tok}")
    endif()
  endwhile()
  if(NOT _saw_failure AND _cmd)
    set(_fixed "")
    foreach(_t IN LISTS _cmd)
      string(REPLACE "<SOURCE_DIR>" "${workdir}" _t "${_t}")
      list(APPEND _fixed "${_t}")
    endforeach()
    execute_process(
      COMMAND ${_fixed}
      WORKING_DIRECTORY "${workdir}"
      RESULT_VARIABLE _rc
      OUTPUT_VARIABLE _out
      ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
      set(_saw_failure TRUE)
      if(expect_success)
        message(FATAL_ERROR
          "generated patch command failed rc=${_rc}\n${_out}${_err}\n"
          "cmd=${_fixed}")
      endif()
    endif()
  endif()
  if(NOT expect_success AND NOT _saw_failure)
    message(FATAL_ERROR
      "expected generated patch command to fail, but it succeeded\n"
      "workdir=${workdir}")
  endif()
endfunction()

message(STATUS "cpm_add_patches_test: applicable patch changes files")
set(CPM_ARGS_UNPARSED_ARGUMENTS "")
cpm_add_patches("${_patches_dir}/01_hello.patch")
set(_tree "${_fixture_root}/t_apply")
apptraverse_seed_tree("${_tree}")
apptraverse_run_patch_command_list("${_tree}" TRUE)
apptraverse_read_file("${_tree}/hello.txt" _hello)
if(NOT _hello STREQUAL "hello-patched\n")
  message(FATAL_ERROR "expected hello-patched, got: ${_hello}")
endif()

message(STATUS "cpm_add_patches_test: re-apply is a no-op success")
file(MD5 "${_tree}/hello.txt" _hash_before)
set(CPM_ARGS_UNPARSED_ARGUMENTS "")
cpm_add_patches("${_patches_dir}/01_hello.patch")
apptraverse_run_patch_command_list("${_tree}" TRUE)
file(MD5 "${_tree}/hello.txt" _hash_after)
if(NOT _hash_before STREQUAL _hash_after)
  message(FATAL_ERROR "re-apply changed file content unexpectedly")
endif()
apptraverse_read_file("${_tree}/hello.txt" _hello)
if(NOT _hello STREQUAL "hello-patched\n")
  message(FATAL_ERROR "re-apply corrupted content: ${_hello}")
endif()
file(GLOB _rejs "${_tree}/*.rej")
if(_rejs)
  message(FATAL_ERROR "re-apply left reject files: ${_rejs}")
endif()

message(STATUS "cpm_add_patches_test: corrupt patch fails")
set(CPM_ARGS_UNPARSED_ARGUMENTS "")
cpm_add_patches("${_patches_dir}/bad_garbage.patch")
set(_tree_bad "${_fixture_root}/t_bad")
apptraverse_seed_tree("${_tree_bad}")
apptraverse_run_patch_command_list("${_tree_bad}" FALSE)
apptraverse_read_file("${_tree_bad}/hello.txt" _hello)
if(NOT _hello STREQUAL "hello\n")
  message(FATAL_ERROR "corrupt patch must not change hello.txt")
endif()

message(STATUS "cpm_add_patches_test: incompatible patch fails")
set(CPM_ARGS_UNPARSED_ARGUMENTS "")
cpm_add_patches("${_patches_dir}/incompatible.patch")
set(_tree_inc "${_fixture_root}/t_incompatible")
apptraverse_seed_tree("${_tree_inc}")
apptraverse_run_patch_command_list("${_tree_inc}" FALSE)
apptraverse_read_file("${_tree_inc}/hello.txt" _hello)
if(NOT _hello STREQUAL "hello\n")
  message(FATAL_ERROR "incompatible patch must not change hello.txt")
endif()

message(STATUS "cpm_add_patches_test: partial patch fails without half-apply")
set(CPM_ARGS_UNPARSED_ARGUMENTS "")
cpm_add_patches("${_patches_dir}/partial.patch")
set(_tree_part "${_fixture_root}/t_partial")
apptraverse_seed_tree("${_tree_part}")
apptraverse_run_patch_command_list("${_tree_part}" FALSE)
apptraverse_read_file("${_tree_part}/hello.txt" _hello)
# dry-run fails as a whole → apply never runs → content unchanged
if(NOT _hello STREQUAL "hello\n")
  message(FATAL_ERROR
    "partial patch must not leave a half-applied tree, got: ${_hello}")
endif()

message(STATUS "cpm_add_patches_test: multi-patch first and second run")
set(CPM_ARGS_UNPARSED_ARGUMENTS "")
cpm_add_patches(
  "${_patches_dir}/01_hello.patch"
  "${_patches_dir}/02_extra.patch")
set(_tree_multi "${_fixture_root}/t_multi")
apptraverse_seed_tree("${_tree_multi}")
apptraverse_run_patch_command_list("${_tree_multi}" TRUE)
apptraverse_read_file("${_tree_multi}/hello.txt" _hello)
apptraverse_read_file("${_tree_multi}/extra.txt" _extra)
if(NOT _hello STREQUAL "hello-patched\n")
  message(FATAL_ERROR "multi first run hello: ${_hello}")
endif()
if(NOT _extra STREQUAL "extra-patched\n")
  message(FATAL_ERROR "multi first run extra: ${_extra}")
endif()
# Second configure/build equivalent: both patches already applied.
file(MD5 "${_tree_multi}/hello.txt" _h1)
file(MD5 "${_tree_multi}/extra.txt" _e1)
set(CPM_ARGS_UNPARSED_ARGUMENTS "")
cpm_add_patches(
  "${_patches_dir}/01_hello.patch"
  "${_patches_dir}/02_extra.patch")
apptraverse_run_patch_command_list("${_tree_multi}" TRUE)
file(MD5 "${_tree_multi}/hello.txt" _h2)
file(MD5 "${_tree_multi}/extra.txt" _e2)
if(NOT _h1 STREQUAL _h2 OR NOT _e1 STREQUAL _e2)
  message(FATAL_ERROR "multi second run changed content")
endif()

message(STATUS "cpm_add_patches_test: OK")
