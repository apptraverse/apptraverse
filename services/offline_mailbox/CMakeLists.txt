cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AppTraverseOfflineMailbox LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  include(../../cmake/CPM.cmake)
  include(CTest)
  option(APPTRAVERSE_BUILD_MAILBOX_STORE "Build RocksDB storage" ON)
endif()
get_filename_component(_mailbox_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_library(apptraverse_offline_mailbox STATIC
  "${_mailbox_root}/src/offline_mailbox.cpp")
target_include_directories(apptraverse_offline_mailbox PUBLIC "${_mailbox_root}/include")
target_compile_features(apptraverse_offline_mailbox PUBLIC cxx_std_20)
if(TARGET apptraverse_compile_policy)
  target_link_libraries(apptraverse_offline_mailbox PUBLIC apptraverse_compile_policy)
endif()
if(APPTRAVERSE_BUILD_MAILBOX_STORE)
  # RocksDB dual-license: this project selects Apache-2.0. Optional compression,
  # benchmarking and flags packages are intentionally not pulled in.
  CPMAddPackage(
    NAME rocksdb
    GITHUB_REPOSITORY facebook/rocksdb
    GIT_TAG c344e30e276b5359f1a0970a00395a0d70a2bf2b
    EXCLUDE_FROM_ALL NO
    OPTIONS
      "ROCKSDB_BUILD_SHARED OFF" "WITH_TESTS OFF" "WITH_BENCHMARK_TOOLS OFF"
      "WITH_CORE_TOOLS OFF" "WITH_TOOLS OFF" "WITH_TRACE_TOOLS OFF"
      "WITH_GFLAGS OFF" "WITH_SNAPPY OFF" "WITH_LZ4 OFF" "WITH_ZLIB OFF"
      "WITH_ZSTD OFF" "WITH_BZ2 OFF" "WITH_LIBURING OFF" "PORTABLE ON"
      "FAIL_ON_WARNINGS OFF" "USE_RTTI OFF")
  add_library(apptraverse_offline_mailbox_rocksdb STATIC
    "${_mailbox_root}/src/offline_mailbox_rocksdb.cpp")
  target_link_libraries(apptraverse_offline_mailbox_rocksdb PUBLIC
    apptraverse_offline_mailbox rocksdb)
endif()
if(BUILD_TESTING)
  add_executable(apptraverse_offline_mailbox_test tests/mailbox_test.cpp)
  target_link_libraries(apptraverse_offline_mailbox_test PRIVATE apptraverse_offline_mailbox)
  add_test(NAME apptraverse_offline_mailbox_test COMMAND apptraverse_offline_mailbox_test)
  if(APPTRAVERSE_BUILD_MAILBOX_STORE)
    add_executable(apptraverse_offline_mailbox_rocksdb_test tests/rocksdb_test.cpp)
    target_link_libraries(apptraverse_offline_mailbox_rocksdb_test PRIVATE apptraverse_offline_mailbox_rocksdb)
    add_test(NAME apptraverse_offline_mailbox_rocksdb_test COMMAND apptraverse_offline_mailbox_rocksdb_test)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME apptraverse_offline_mailbox_crash_test
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/crash_recovery.py
              $<TARGET_FILE:apptraverse_offline_mailbox_rocksdb_test>)
  endif()
endif()
