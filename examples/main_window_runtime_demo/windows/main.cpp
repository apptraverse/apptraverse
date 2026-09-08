#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "apptraverse/noninteractive_crt.h"

#include "main_window_lifecycle.h"
#include "main_window_model.h"
#include "win_app.h"

int main(int argc, char** argv) {
  apptraverse::EnableNoninteractiveCrt();
  std::filesystem::path state_dir{"main_window_runtime_state"};
  int hold_stage = -1;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--hold-stage" && i + 1 < argc) {
      hold_stage = std::stoi(argv[++i]);
    }
  }
  apptraverse::EnsureMainWindowRegistration();
  apptraverse::WinApp app;
  if (hold_stage >= 0) {
    app.SetHoldStage(static_cast<apptraverse::ModelStartupStage>(hold_stage));
  }
  return app.Run(state_dir);
}
