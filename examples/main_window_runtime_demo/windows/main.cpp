#include <filesystem>
#include <string_view>

#include "main_window_model.h"
#include "win_app.h"
#include "win_presenters.h"

int main(int argc, char** argv) {
  std::filesystem::path state_dir{"main_window_runtime_state"};
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    }
  }
  apptraverse::EnsureMainWindowRegistration();
  apptraverse::EnsureWin32MainWindowPresenterRegistration();
  apptraverse::WinApp app;
  return app.Run(state_dir);
}
