#include <filesystem>
#include <string_view>

#include <unistd.h>

#include "mac_app.h"
#include "mac_presenters.h"

int main(int argc, char** argv) {
  std::filesystem::path state_dir{"surfaces_runtime_state"};
  bool foreground = false;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--foreground") {
      // Stay in the parent session (Ctrl-C / shell job control). Default is
      // to detach so agent-tool / Terminal session end does not kill the GUI.
      foreground = true;
    }
  }

  if (!foreground) {
    // Leave the launching process group: Cursor agent shells and job-control
    // SIGHUP otherwise reap a backgrounded GUI before the user can check it.
    pid_t const pid = fork();
    if (pid < 0) {
      return 1;
    }
    if (pid > 0) {
      return 0;
    }
    (void)setsid();
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSurfacesModelRegistration();
  apptraverse::EnsureMacSurfacePresenterRegistration();
  apptraverse::MacApp app;
  return app.Run(state_dir);
}
