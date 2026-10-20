#include <filesystem>
#include <string_view>

#include "apptraverse/surfaces/surfaces_registration.h"

#include "linux_app.h"
#include "surfaces_state_dir.h"
#include "linux_presenters.h"

int main(int argc, char** argv) {
  std::filesystem::path state_dir =
      apptraverse::surfaces_demo::DefaultRuntimeStateDir();
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    }
  }
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSurfacesRegistration();
  apptraverse::EnsureLinuxSurfacePresenterRegistration();
  apptraverse::LinuxApp app;
  return app.Run(state_dir);
}
