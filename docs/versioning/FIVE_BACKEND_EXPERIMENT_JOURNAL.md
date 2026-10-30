# Five-backend WindowCatalog experiment — working journal

**Branch:** `experiment/window-catalog-five-backends-982b473`  
**Checkpoint tag:** `checkpoint/web4-before-five-backends-982b473` @ `982b473`

## Matrix (live)

| Backend | Build | Runtime E2E | Save + cold reload | First open failure |
|--------|-------|---------------|--------------------|--------------------|
| WinAPI | incremental OK (VsDevShell + CMake cache regen) | pending | pending | — |
| Headless | OK | **PASS** `apptraverse_window_catalog_headless_test` | in-test + cold `LoadApplication` session | — |
| WebAssembly | OK | **PASS** `wasm_window_catalog_browser_smoke.py` full E2E + IDBFS reload | IDBFS `/persistent/window_catalog_web_five_backend_state` | — |
| GTK3 / WSL | not started | — | — | — |
| Android JNI | not started | — | — | — |

## Next operation

1. Run `apptraverse_window_catalog_win32_acceptance_test` + manual Win catalog E2E in VsDevCmd.  
2. Implement `apptraverse_ui_linux` + `linux_window_catalog_demo`; `build/linux-ninja-debug` under WSLg.  
3. Android JNI WindowCatalog app + emulator E2E.

## Environment notes

- Win64: `CMakeCache.txt` deleted + `cmake --preset win64-ninja-msvc-debug` inside **VsDevShell** with `msys64` stripped from `PATH` (msys `ar`/`ld` broke MSVC link).  
- Builds: `cmd /c "call ...\\VsDevCmd.bat -arch=amd64 && cmake --build build\\win64-ninja-msvc-debug --target …"`
