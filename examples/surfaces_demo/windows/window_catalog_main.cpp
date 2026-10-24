#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#undef RegisterClass
#endif

#include <filesystem>
#include <string_view>

#include "apptraverse/noninteractive_crt.h"

#include "window_catalog_win_app.h"

namespace {

std::filesystem::path DefaultStateDir() {
  wchar_t module_path[MAX_PATH]{};
  DWORD const len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return std::filesystem::path{"surfaces_catalog_v0_state"};
  }
  return std::filesystem::path{module_path}.parent_path() /
         "surfaces_catalog_v0_state";
}

}  // namespace

int main(int argc, char** argv) {
  FreeConsole();
  apptraverse::EnableNoninteractiveCrt();

  std::filesystem::path state_dir = DefaultStateDir();
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    }
  }

  apptraverse::WindowCatalogWinApp app;
  return app.Run(state_dir);
}
