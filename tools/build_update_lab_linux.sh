#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-"$repo/build/linux-update-lab"}
cmake -S "$repo" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
  -DAPPTRAVERSE_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DAPPTRAVERSE_BUILD_UPDATE_LAB=ON \
  -DAPPTRAVERSE_BUILD_AETHER_LINK=ON -DAPPTRAVERSE_AETHER_STREAM_HIERARCHY=OFF \
  -DAPPTRAVERSE_AETHER_USE_WORKSPACE_SOURCE=OFF \
  -DAPPTRAVERSE_AETHER_USER_CONFIG="$repo/cmake/update_lab_aether_config.h" \
  -DAE_NO_DEBUG_LOG=ON -DAE_OBJECTS_RAW_TELE=OFF "${@:2}"
if [[ -n ${UPDATE_LAB_PROVIDER_BUNDLE:-} ]]; then
  [[ $(uname -m) == x86_64 ]] || { echo "Provider bundle requires Linux x86-64" >&2; exit 1; }
  cmake --build "$build" --target update_lab_provider update_lab_state_test -j 8
  "$build/examples/update_lab/update_lab_state_test"
  mkdir -p "$build/lifecycle-provider/runtime"
  stage="$build/lifecycle-provider"
  strip --strip-debug -o "$stage/update_lab_provider" "$build/examples/update_lab/update_lab_provider"
  # Bundle the loader and resolved libraries to support an older test VM.
  while IFS= read -r library; do
    cp -L -- "$library" "$stage/runtime/"
  done < <(ldd "$stage/update_lab_provider" | awk '/=> \/.* / { print $3 } /^[[:space:]]*\// { print $1 }')
  test -x "$stage/runtime/ld-linux-x86-64.so.2"
  "$stage/runtime/ld-linux-x86-64.so.2" --library-path "$stage/runtime" --list "$stage/update_lab_provider"
  cp "$repo/services/update_lifecycle/apptraverse-lifecycle.service" "$stage/"
  tar -C "$stage" -czf "$UPDATE_LAB_PROVIDER_BUNDLE" update_lab_provider runtime apptraverse-lifecycle.service
  exit
fi
cmake --build "$build" --target update_lab_pack update_lab_provider update_lab_launcher update_lab_e0 update_lab_e1 update_lab_e2 update_lab_state_test -j 8
"$build/examples/update_lab/update_lab_state_test"
mkdir -p "$build/update-lab-package"
catalog="$build/_deps/aether-client-cpp-build/generated/aether-catalog"
telemetry=()
if [[ -f "$catalog/build.json" ]]; then
  mkdir -p "$build/update-lab-package/aether-catalog"
  cp -r "$catalog/build.json" "$catalog/catalogs" "$build/update-lab-package/aether-catalog/"
  telemetry=(--linux-x64-e0-telemetry "$build/update-lab-package/aether-catalog" --linux-x64-e1-telemetry "$build/update-lab-package/aether-catalog")
fi
strip --strip-debug -o "$build/update-lab-package/update_lab_launcher" "$build/examples/update_lab/update_lab_launcher"
for revision in 0 1 2; do
  # Retain the Debug build and its symbols; only delivery copies lose debug data.
  objcopy --only-keep-debug "$build/examples/update_lab/update_lab_e$revision" "$build/update-lab-package/update_lab_e$revision.debug"
  strip --strip-debug -o "$build/update-lab-package/update_lab_e$revision" \
    "$build/examples/update_lab/update_lab_e$revision"
done
"$build/examples/update_lab/update_lab_provider" publish --out "${UPDATE_LAB_STORE:-$repo/artifacts/update-lab/lzma1-releases-linux}" \
  --linux-x64-launcher "$build/update-lab-package/update_lab_launcher" \
  --linux-x64-e0 "$build/update-lab-package/update_lab_e0" \
  --linux-x64-e1 "$build/update-lab-package/update_lab_e1" \
  --linux-x64-e0-symbols "$build/update-lab-package/update_lab_e0.debug" \
  --linux-x64-e1-symbols "$build/update-lab-package/update_lab_e1.debug" \
  --linux-x64-e2 "$build/update-lab-package/update_lab_e2" "${telemetry[@]}"
