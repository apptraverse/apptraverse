#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <string_view>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/windows/win_application_host.h"

#include "minimal_app_desktop_close.h"
#include "minimal_app_lifecycle.h"
#include "minimal_app_registration.h"
#include "minimal_application.h"
#include "apptraverse/ui/ui_window_presenter.h"
#if defined(APPTRAVERSE_MINIMAL_APP_DEBUG_BOOTSTRAP)
#include "minimal_app_distillation.h"
#endif

namespace {

std::filesystem::path DefaultStateDir() {
  wchar_t module_path[MAX_PATH]{};
  DWORD const len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    apptraverse::WriteFatalStderr("fatal: cannot resolve application directory\n");
    std::abort();
  }
  return std::filesystem::path{module_path}.parent_path() /
         "minimal_app_s1_controls_state";
}

}  // namespace

int main(int argc, char** argv) {
  FreeConsole();
  apptraverse::example::minimal::RegisterMinimalApplicationTypes();

  std::filesystem::path state_dir = DefaultStateDir();
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
#if defined(APPTRAVERSE_MINIMAL_APP_DEBUG_BOOTSTRAP)
    if (arg == "--distill-only" && i + 1 < argc) {
      apptraverse::example::minimal::DistillMinimalApplication(argv[++i]);
      return 0;
    }
#endif
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    }
  }

  apptraverse::ApplicationModelRuntime runtime;
  int model_exit_status = 1;

  apptraverse::windows::WinApplicationHostCallbacks callbacks;
  callbacks.run_model = [&](apptraverse::ApplicationPublishedCallback on_published) {
    model_exit_status = apptraverse::example::minimal::RunMinimalApplicationModel(
        state_dir, runtime, on_published);
  };
  callbacks.request_stop = [&] { runtime.RequestStop(); };
  auto report_environment = [](ae::Obj& root) {
    auto& application = static_cast<apptraverse::example::minimal::MinimalApplication&>(root);
    auto& presenter = *application.main_window->presenter;
    auto hwnd = static_cast<HWND>(presenter.NativeChildParentHandle());
    auto dpi = static_cast<std::int32_t>(GetDpiForWindow(hwnd) * 160 / 96);
    if (!application.viewport.hosted && !application.viewport.single_page && application.viewport.density_dpi == dpi) return;
    presenter.model_proxy->Invoke(root.obj_id,
        &apptraverse::example::minimal::MinimalApplication::SubmitDesktopEnvironment, dpi);
  };
  callbacks.after_initial = report_environment;
  callbacks.after_incremental = report_environment;
  callbacks.on_desktop_window_close =
      [&](ae::Obj& root, ae::Obj& closed_window,
          apptraverse::ModelObjectProxy& proxy) {
        apptraverse::example::minimal::HandleMinimalAppDesktopWindowClose(
            root, closed_window, proxy, [&] { runtime.RequestStop(); });
      };

  apptraverse::windows::WinApplicationLoadingOptions loading{
      L"AppTraverseMinimalAppLoading", L"Loading", L"Loading", 200, 200, 320,
      120};

  apptraverse::windows::WinApplicationHost host(runtime, loading,
                                              std::move(callbacks));
  host.Run();
  return model_exit_status;
}
