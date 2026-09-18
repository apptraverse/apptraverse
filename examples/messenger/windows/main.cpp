#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <filesystem>
#include <string_view>

#include "apptraverse/noninteractive_crt.h"

#include "join_delivery_trace.h"
#include "win_app.h"
#include "win_presenters.h"

int main(int argc, char** argv) {
  // GUI host: no debug console even when launched from a terminal/parent.
  FreeConsole();
  apptraverse::EnableNoninteractiveCrt();

  // Join/write delivery trace → file (stderr is unavailable after FreeConsole).
  // Set APPTRAVERSE_JOIN_TRACE to a path before launch; A/B must use different
  // files.
  apptraverse::example::chat_demo::EnableJoinDeliveryTraceFromEnv();

  std::filesystem::path state_dir{"messenger_runtime_state"};
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    }
  }
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureMessengerModelRegistration();
  apptraverse::EnsureWin32SurfacePresenterRegistration();
  apptraverse::WinApp app;
  return app.Run(state_dir);
}
