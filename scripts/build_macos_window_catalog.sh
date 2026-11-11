#!/bin/bash
# Incremental Debug build of the Apple WindowCatalog slice (not legacy demos).
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${1:-$repo_dir/build/macos-debug}"
cmake_bin="${CMAKE:-cmake}"
clang_c="${CC:-/opt/local/bin/clang-mp-20}"
clang_cxx="${CXX:-/opt/local/bin/clang++-mp-20}"
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
"$cmake_bin" -S "$repo_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER="$clang_c" -DCMAKE_CXX_COMPILER="$clang_cxx" \
  -DCMAKE_OBJC_COMPILER="$clang_c" -DCMAKE_OBJCXX_COMPILER="$clang_cxx" \
  -DCMAKE_OSX_SYSROOT="$sdk_path" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DAE_NO_DEBUG_LOG=ON
"$cmake_bin" --build "$build_dir" --target \
  macos_window_catalog_demo macos_window_catalog_demo_load_only \
  apptraverse_macos_window_catalog_acceptance_debug apptraverse_macos_window_catalog_acceptance_load_only \
  apptraverse_macos_ui_backend_lifecycle_test \
  apptraverse_window_catalog_headless_test \
  apptraverse_aether_domain_hierarchy_test apptraverse_object_clone_test \
  apptraverse_single_file_user_storage_test \
  apptraverse_nested_window_frame_publication_test \
  apptraverse_application_contract_headless_test \
  apptraverse_historical_base_materialization_test \
  --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
ctest --test-dir "$build_dir" --output-on-failure \
  -R '^apptraverse_(macos_.*|window_catalog_headless_test|aether_domain_hierarchy_test|object_clone_test|single_file_user_storage_test|nested_window_frame_publication_test|application_contract_headless_test|historical_base_materialization_test)$'
