#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "apptraverse/noninteractive_crt.h"

#include "win_app.h"
#include "surfaces_evolution_ops.h"
#include "surfaces_session_automation.h"
#include "surfaces_shutdown_trace.h"
#include "apptraverse/platform/windows/surfaces_win_registration.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/surfaces/surfaces_registration.h"
#include "apptraverse/ui/ui_presenters.h"
#include "surfaces_demo_win32_registration.h"

int main(int argc, char** argv) {
  std::filesystem::path state_dir{"surfaces_runtime_state"};
  std::string evolution_op;
  std::string session_automation;
  bool session_inspect = false;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--evolution-op" && i + 1 < argc) {
      evolution_op = argv[++i];
    } else if (arg == "--session-automation" && i + 1 < argc) {
      session_automation = argv[++i];
    } else if (arg == "--session-inspect") {
      session_inspect = true;
    }
  }
  if (session_inspect) {
    apptraverse::EnableNoninteractiveCrt();
    try {
      return apptraverse::RunSurfacesSessionInspect(state_dir);
    } catch (std::exception const& error) {
      std::cerr << "FAIL: " << error.what() << '\n';
      return 1;
    }
  }
  if (!evolution_op.empty()) {
    apptraverse::EnableNoninteractiveCrt();
    try {
      return apptraverse::RunSurfacesEvolutionOp(state_dir, evolution_op);
    } catch (std::exception const& error) {
      std::cerr << "FAIL: " << error.what() << '\n';
      return 1;
    }
  }

  if (!apptraverse::SurfacesShutdownTrace::Enabled()) {
    // GUI host: no debug console even when launched from a terminal/parent.
    FreeConsole();
  }
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSurfacesRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::ui::windows::EnsureWinUiPresenterRegistration();
  apptraverse::EnsureWin32SurfacesPresenterRegistration();
  apptraverse::example::surfaces::EnsureSurfacesDemoWin32Registration();
  apptraverse::WinApp app;
  if (!session_automation.empty()) {
    app.SetSessionAutomation(
        apptraverse::ParseSurfacesSessionAutomation(session_automation));
  }
  int const code = app.Run(state_dir);
  apptraverse::SurfacesShutdownTrace::MarkProcessExit();
  apptraverse::SurfacesShutdownTrace::FlushSummary();
  return code;
}
