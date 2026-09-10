#include <filesystem>
#include <string_view>

#include "linux_app.h"
#include "linux_presenters.h"

int main(int argc, char** argv) {
  std::filesystem::path state_dir{"surfaces_runtime_state"};
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    }
  }
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSurfacesModelRegistration();
  apptraverse::EnsureLinuxSurfacePresenterRegistration();
  apptraverse::LinuxApp app;
  return app.Run(state_dir);
}
