#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <string_view>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/windows/win_application_host.h"

#include "minimal_app_lifecycle.h"

namespace {

std::filesystem::path DefaultStateDir() {
  wchar_t module_path[MAX_PATH]{};
  DWORD const len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return std::filesystem::path{"minimal_app_s1_controls_state"};
  }
  return std::filesystem::path{module_path}.parent_path() /
         "minimal_app_s1_controls_state";
}

}  // namespace

int main(int argc, char** argv) {
  FreeConsole();
  apptraverse::EnableNoninteractiveCrt();

  std::filesystem::path state_dir = DefaultStateDir();
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
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
  callbacks.on_desktop_window_close = [&](ae::Obj&, ae::Obj&,
                                          apptraverse::ModelObjectProxy&) {
    runtime.RequestStop();
  };

  apptraverse::windows::WinApplicationLoadingOptions loading{
      L"AppTraverseMinimalAppLoading", L"Loading", L"Loading", 200, 200, 320,
      120};

  apptraverse::windows::WinApplicationHost host(runtime, loading,
                                              std::move(callbacks));
  host.Run();
  return model_exit_status;
}
