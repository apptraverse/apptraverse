#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include "demo_bootstrap.h"
#include "demo_model.h"
#include "win_app.h"

int main(int argc, char** argv) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  bool distill = false;
  std::filesystem::path state_dir{"model_ui_runtime_state"};
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--distill") {
      distill = true;
      if (i + 1 < argc) {
        state_dir = argv[++i];
      }
    } else if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    }
  }

  apptraverse::EnsureDemoRegistration();
  if (distill) {
    apptraverse::DistillDemo(state_dir);
    return 0;
  }
  if (!std::filesystem::exists(state_dir)) {
    apptraverse::DistillDemo(state_dir);
  }
  apptraverse::WinApp app;
  return app.Run(state_dir);
}
