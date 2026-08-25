#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "demo_bootstrap.h"
#include "demo_model.h"
#include "win_app.h"

int main(int argc, char** argv) {
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
