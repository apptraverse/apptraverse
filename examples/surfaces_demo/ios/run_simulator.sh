#!/usr/bin/env bash
# Configure (first run only), build incrementally, install and launch the
# surfaces demo in an installed iPhone simulator. No simulator UUID is
# hardcoded: the first available iPhone of the installed runtime is used.
#
# Extra CMake arguments are forwarded to the first configure, e.g. local
# CPM source overrides.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
developer_dir=${DEVELOPER_DIR:-$(xcode-select -p)}
simctl=$developer_dir/usr/bin/simctl
sdk=${APPTRAVERSE_IOS_SDK:-$developer_dir/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk}
arch=$(uname -m)
deployment_target=${APPTRAVERSE_IOS_DEPLOYMENT_TARGET:-17.0}
build_dir=${APPTRAVERSE_IOS_BUILD_DIR:-$repo_root/build/ios-sim-$arch-debug}
target=${APPTRAVERSE_IOS_TARGET:-ios_surfaces_demo}

# Pinned aether-miscpp needs P0960 parenthesized aggregate initialization,
# which Apple Clang 15 does not implement.
cxx=${APPTRAVERSE_IOS_CXX:-}
if [[ -z $cxx ]]; then
  for candidate in clang++-mp-20 clang++-mp-19 clang++-mp-18; do
    if command -v "$candidate" >/dev/null 2>&1; then
      cxx=$(command -v "$candidate")
      break
    fi
  done
fi
if [[ -z $cxx ]]; then
  echo "No Clang >= 16 found; set APPTRAVERSE_IOS_CXX" >&2
  exit 1
fi
cc=${APPTRAVERSE_IOS_CC:-${cxx%clang++*}clang${cxx#*clang++}}

if [[ ! -f $build_dir/CMakeCache.txt ]]; then
  cmake -S "$repo_root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$sdk" \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
    -DCMAKE_C_COMPILER="$cc" \
    -DCMAKE_CXX_COMPILER="$cxx" \
    -DCMAKE_OBJCXX_COMPILER="$cxx" \
    -DAPPTRAVERSE_BUILD_AETHER_DEMOS=OFF \
    -DBUILD_TESTING=OFF \
    "$@"
fi
cmake --build "$build_dir" --target "$target"

app=$build_dir/examples/surfaces_demo/ios/$target.app
codesign --force --sign - "$app"
bundle_id=$(plutil -extract CFBundleIdentifier raw "$app/Info.plist")

device=${APPTRAVERSE_IOS_DEVICE:-}
if [[ -z $device ]]; then
  device=$("$simctl" list devices available |
    grep -E '^ +iPhone' |
    grep -oE '[0-9A-Fa-f]{8}-([0-9A-Fa-f]{4}-){3}[0-9A-Fa-f]{12}' |
    head -1)
fi

"$simctl" boot "$device" || true
"$simctl" bootstatus "$device" -b
"$simctl" install "$device" "$app"
echo "app=$app"
echo "bundle_id=$bundle_id"
echo "device=$device"
# APPTRAVERSE_IOS_LAUNCH_ARGS lets a test run point the app at its own
# `--state-dir <path>` instead of the app-local Application Support directory.
# shellcheck disable=SC2086
"$simctl" launch "$device" "$bundle_id" ${APPTRAVERSE_IOS_LAUNCH_ARGS:-}
