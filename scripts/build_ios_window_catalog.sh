#!/bin/bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
host_build="${1:-$repo_dir/build/ios-host-debug}"
ios_build="${2:-$repo_dir/build/ios-simulator-debug}"
cmake_bin="${CMAKE:-cmake}"
clang_c="${CC:-/opt/local/bin/clang-mp-20}"
clang_cxx="${CXX:-/opt/local/bin/clang++-mp-20}"
"$cmake_bin" -S "$repo_dir" -B "$host_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER="$clang_c" -DCMAKE_CXX_COMPILER="$clang_cxx" \
  -DCMAKE_OBJC_COMPILER="$clang_c" -DCMAKE_OBJCXX_COMPILER="$clang_cxx" \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DAE_NO_DEBUG_LOG=ON
"$cmake_bin" --build "$host_build" --target apptraverse_export_window_catalog_ios \
  --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
exporter="$host_build/examples/surfaces_demo/ios/host_export/apptraverse_export_window_catalog_ios"
"$cmake_bin" -S "$repo_dir" -B "$ios_build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$repo_dir/cmake/toolchains/ios-simulator.cmake" \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF -DAPPTRAVERSE_BUILD_IOS_ACCEPTANCE=ON \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DAE_NO_DEBUG_LOG=ON \
  -DCPM_SOURCE_CACHE="$host_build/cpm.cache" \
  -DAPPTRAVERSE_WINDOW_CATALOG_IOS_HOST_EXPORTER="$exporter" \
  -DAPPTRAVERSE_IOS_HOST_EXPORTER="$host_build/examples/minimal_app/ios/host_export/apptraverse_export_minimal_distilled_graph_ios"
"$cmake_bin" --build "$ios_build" --target ios_window_catalog_demo ios_window_catalog_demo_load_only \
  apptraverse_ios_window_catalog_acceptance_debug apptraverse_ios_window_catalog_acceptance_load_only \
  --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
