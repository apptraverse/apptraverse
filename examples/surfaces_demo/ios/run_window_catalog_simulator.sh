#!/usr/bin/env bash
# Build and launch ios_window_catalog_demo on the newest installed iOS
# Simulator runtime that is available, then the highest iPhone model on that
# runtime. Does not take the first iPhone from `grep | head`, and does not
# treat the installed SDK version as a substitute for an installed runtime.
#
# Deployment target and compiler triples follow the chosen runtime. C/C++/ObjC++
# use an absolute MacPorts clang. Swift stays on the selected Xcode swiftc.
# This script does not change PATH, xcode-select, or port select.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
developer_dir=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}
simctl=$developer_dir/usr/bin/simctl
arch=$(uname -m)
build_dir=${APPTRAVERSE_IOS_BUILD_DIR:-$repo_root/build/ios-sim-$arch-debug}
target=${APPTRAVERSE_IOS_TARGET:-ios_window_catalog_demo}

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
  echo "No MacPorts clang++ >= 18 found; set APPTRAVERSE_IOS_CXX" >&2
  exit 1
fi
cc=${APPTRAVERSE_IOS_CC:-${cxx%clang++*}clang${cxx#*clang++}}
swiftc=${APPTRAVERSE_IOS_SWIFTC:-$developer_dir/Toolchains/XcodeDefault.xctoolchain/usr/bin/swiftc}

selection=$(python3 - "$simctl" << 'PY'
import json, subprocess, sys
simctl = sys.argv[1]
runtimes = json.loads(subprocess.check_output([simctl, "list", "runtimes", "-j"]))
devices = json.loads(subprocess.check_output([simctl, "list", "devices", "available", "-j"]))

def version_key(text):
    parts = []
    for piece in str(text).split("."):
        digits = "".join(ch for ch in piece if ch.isdigit())
        parts.append(int(digits) if digits else -1)
    return tuple(parts)

chosen = None
for runtime in runtimes.get("runtimes", []):
    if not runtime.get("isAvailable"):
        continue
    identifier = runtime.get("identifier", "")
    if "iOS" not in identifier:
        continue
    version = runtime.get("version") or ""
    key = version_key(version)
    if chosen is None or key > chosen[0]:
        chosen = (key, version, identifier, runtime.get("name", ""))
if chosen is None:
    sys.exit("no available iOS simulator runtime")
_, version, runtime_id, runtime_name = chosen

def model_rank(name):
    # Higher iPhone generation first. Pro Max outranks Pro, Plus, and SE.
    lowered = name.lower()
    if not lowered.startswith("iphone"):
        return None
    number = 0
    for token in name.replace("(", " ").replace(")", " ").split():
        if token.isdigit():
            number = int(token)
            break
    tier = 1
    if "pro max" in lowered:
        tier = 4
    elif "pro" in lowered:
        tier = 3
    elif "plus" in lowered:
        tier = 2
    elif lowered.startswith("iphone se"):
        tier = 0
        number = 0
    return (number, tier, name)

best = None
for runtime_key, group in devices.get("devices", {}).items():
    if runtime_id not in runtime_key and runtime_id.split(".")[-1] not in runtime_key:
        continue
    for device in group:
        if not device.get("isAvailable", True):
            continue
        rank = model_rank(device.get("name", ""))
        if rank is None:
            continue
        candidate = (rank, device["udid"], device["name"])
        if best is None or candidate[0] > best[0]:
            best = candidate
if best is None:
    sys.exit(f"no available iPhone for {runtime_id}")
_, udid, name = best
print(f"{version}\t{runtime_id}\t{runtime_name}\t{name}\t{udid}")
PY
)

IFS=$'\t' read -r runtime_version runtime_id runtime_name device_name device_udid <<< "$selection"
sdk=$developer_dir/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator${runtime_version}.sdk
if [[ ! -d $sdk ]]; then
  sdk=$developer_dir/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk
fi
triple=${arch}-apple-ios${runtime_version}-simulator

echo "runtime=$runtime_name $runtime_version $runtime_id"
echo "device=$device_name $device_udid"
echo "sdk=$sdk"
echo "triple=$triple"
echo "cxx=$cxx"
echo "swiftc=$swiftc"

if [[ ${1:-} == --print-device ]]; then
  exit 0
fi

if [[ ! -f $build_dir/CMakeCache.txt ]]; then
  cmake -S "$repo_root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$sdk" \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$runtime_version" \
    -DCMAKE_C_COMPILER="$cc" \
    -DCMAKE_CXX_COMPILER="$cxx" \
    -DCMAKE_OBJC_COMPILER="$cc" \
    -DCMAKE_OBJCXX_COMPILER="$cxx" \
    -DCMAKE_Swift_COMPILER="$swiftc" \
    -DCMAKE_C_COMPILER_TARGET="$triple" \
    -DCMAKE_CXX_COMPILER_TARGET="$triple" \
    -DCMAKE_OBJC_COMPILER_TARGET="$triple" \
    -DCMAKE_OBJCXX_COMPILER_TARGET="$triple" \
    -DCMAKE_Swift_COMPILER_TARGET="$triple" \
    -DAPPTRAVERSE_BUILD_LINUX_GUI=OFF \
    -DBUILD_TESTING=OFF
fi
cmake --build "$build_dir" --target "$target"

app=$build_dir/examples/surfaces_demo/ios/$target.app
codesign --force --sign - "$app"
bundle_id=$(plutil -extract CFBundleIdentifier raw "$app/Info.plist")
"$simctl" boot "$device_udid" || true
"$simctl" bootstatus "$device_udid" -b
"$simctl" install "$device_udid" "$app"
echo "app=$app"
echo "bundle_id=$bundle_id"
"$simctl" launch "$device_udid" "$bundle_id" ${APPTRAVERSE_IOS_LAUNCH_ARGS:-}
