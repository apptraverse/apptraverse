#include <filesystem>
#include <string_view>

#include "win_app.h"

int main(int argc, char** argv) {
  std::filesystem::path state_dir{"dynamic_objects_runtime_state"};
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    }
  }
  apptraverse::EnsureObjectRegistration();
  apptraverse::WinApp app;
  return app.Run(state_dir);
}
