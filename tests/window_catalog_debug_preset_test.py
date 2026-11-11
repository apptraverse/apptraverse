"""Check the declared VS Debug preset against the real Catalog CMake targets.

Generate a small multi-config project with the existing Windows compiler. It
includes the application's CMakeLists directly, without fetching dependencies
or building another application tree. The probe directory is reused on reruns.
"""

import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--ninja", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--debug-flags", required=True)
    args = parser.parse_args()
    source = args.source.resolve()
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    presets = json.loads((source / "CMakePresets.json").read_text(encoding="utf-8"))
    preset_name = "win64-vs2022-msvc-debug"
    preset = next(p for p in presets["configurePresets"] if p["name"] == preset_name)
    build = next(p for p in presets["buildPresets"] if p["name"] == preset_name)
    if build["configuration"] != "Debug":
        raise AssertionError("The Windows Debug preset must build Debug")

    # A placeholder is sufficient: configure must never consume its contents.
    supply = work / "prepared_supply.h"
    supply.write_text("// configure-only supply placeholder\n", encoding="utf-8")
    fixture = work / "source"
    fixture.mkdir(exist_ok=True)
    catalog_cmake = (source / "examples/surfaces_demo/windows").as_posix()
    (fixture / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.20)\n'
        'project(WindowCatalogDebugPresetProbe LANGUAGES CXX)\n'
        f'add_subdirectory("{catalog_cmake}" catalog-windows)\n'
        'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/catalog-$<CONFIG>.txt" CONTENT\n'
        '"definitions=$<TARGET_PROPERTY:win32_window_catalog_demo,COMPILE_DEFINITIONS>\n'
        'sources=$<TARGET_PROPERTY:win32_window_catalog_demo,SOURCES>\n'
        'dependencies=$<TARGET_PROPERTY:win32_window_catalog_demo,MANUALLY_ADDED_DEPENDENCIES>\n'
        'consumer_definitions=$<TARGET_PROPERTY:win32_window_catalog_demo_load_only,COMPILE_DEFINITIONS>\n'
        'consumer_dependencies=$<TARGET_PROPERTY:win32_window_catalog_demo_load_only,MANUALLY_ADDED_DEPENDENCIES>\n'
        'configurations=${CMAKE_CONFIGURATION_TYPES}\n")\n', encoding="utf-8")
    binary = work / "build"
    command = [args.cmake, "-S", str(fixture), "-B", str(binary),
               "-G", "Ninja Multi-Config", f"-DCMAKE_MAKE_PROGRAM={args.ninja}",
               f"-DCMAKE_CXX_COMPILER={args.compiler}",
               f"-DCMAKE_CXX_FLAGS_DEBUG={args.debug_flags}",
               f"-DAPPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER={supply}"]
    # Do not let a previous passing probe supply settings removed from the preset.
    command += ["-UCMAKE_BUILD_TYPE", "-UCMAKE_CONFIGURATION_TYPES"]
    command += [f"-D{key}={value}" for key, value in preset.get("cacheVariables", {}).items()]
    subprocess.run(command, check=True)
    report = dict(line.split("=", 1) for line in
                  (binary / "catalog-Debug.txt").read_text(encoding="utf-8").splitlines())
    required = "APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP"
    embedded = "APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL"
    assert required in report["definitions"], report
    assert embedded not in report["definitions"], report
    assert "window_catalog_distillation.cpp" in report["sources"], report
    assert "apptraverse_window_catalog_embedded_supply" not in report["dependencies"], report
    assert embedded in report["consumer_definitions"], report
    assert "APPTRAVERSE_WINDOW_CATALOG_LOAD_ONLY" in report["consumer_definitions"], report
    assert "apptraverse_window_catalog_embedded_supply" in report["consumer_dependencies"], report
    assert report["configurations"] == "Debug", report
    print("WINDOW_CATALOG_DEBUG_PRESET ok fresh_bootstrap=1 load_only_consumer=1")


if __name__ == "__main__":
    main()
