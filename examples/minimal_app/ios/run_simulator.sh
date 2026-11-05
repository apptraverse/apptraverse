#!/usr/bin/env bash
# Incremental iOS sim build, install, and launch MinimalApp on the newest
# installed iPhone simulator (prefers iPhone 15 Pro Max when present).
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
developer_dir=${DEVELOPER_DIR:-$(xcode-select -p)}
simctl=$developer_dir/usr/bin/simctl
sdk=${APPTRAVERSE_IOS_SDK:-$developer_dir/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk}
arch=$(uname -m)
deployment_target=${APPTRAVERSE_IOS_DEPLOYMENT_TARGET:-17.0}
build_dir=${APPTRAVERSE_IOS_BUILD_DIR:-$repo_root/build/ios-sim-$arch-debug}
mac_build_dir=${APPTRAVERSE_MACOS_BUILD_DIR:-$repo_root/build/macos-x64-mp20-debug}
target=apptraverse_minimal_app_ios
exporter=${APPTRAVERSE_IOS_HOST_EXPORTER:-$mac_build_dir/examples/minimal_app/ios/host_export/apptraverse_export_minimal_distilled_graph_ios}

cxx=${APPTRAVERSE_IOS_CXX:-/opt/local/bin/clang++-mp-20}
cc=${APPTRAVERSE_IOS_CC:-/opt/local/bin/clang-mp-20}

if [[ ! -x $exporter ]]; then
  echo "Host exporter missing; build macOS target apptraverse_export_minimal_distilled_graph_ios first" >&2
  exit 1
fi

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
    -DAPPTRAVERSE_IOS_HOST_EXPORTER="$exporter" \
    -DBUILD_TESTING=OFF \
    "$@"
fi
cmake --build "$build_dir" --target "$target"

app=$build_dir/examples/minimal_app/ios/$target.app
codesign --force --sign - "$app"
bundle_id=$(plutil -extract CFBundleIdentifier raw "$app/Info.plist")

device=${APPTRAVERSE_IOS_DEVICE:-}
if [[ -z $device ]]; then
  device=$("$simctl" list devices available |
    awk '/iPhone 15 Pro Max/ {print; exit}' |
    grep -oE '[0-9A-Fa-f]{8}-([0-9A-Fa-f]{4}-){3}[0-9A-Fa-f]{12}' || true)
fi
if [[ -z $device ]]; then
  device=$("$simctl" list devices available |
    grep -E '^ +iPhone' |
    grep -oE '[0-9A-Fa-f]{8}-([0-9A-Fa-f]{4}-){3}[0-9A-Fa-f]{12}' |
    tail -1)
fi

"$simctl" boot "$device" || true
"$simctl" bootstatus "$device" -b
"$simctl" install "$device" "$app"
echo "app=$app"
echo "bundle_id=$bundle_id"
echo "device=$device"
# shellcheck disable=SC2086
"$simctl" launch --console-pty "$device" "$bundle_id" ${APPTRAVERSE_IOS_LAUNCH_ARGS:-}
