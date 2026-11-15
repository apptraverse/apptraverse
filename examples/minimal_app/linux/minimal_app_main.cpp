#include <filesystem>
#include <iostream>
#include <string_view>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/linux/linux_application_host.h"

#include "minimal_app_desktop_close.h"
#include "minimal_app_lifecycle.h"
#ifdef APPTRAVERSE_MINIMAL_LINUX_TEST_HOOKS
#include "minimal_app_linux_session_automation.h"
#endif
#include "minimal_application.h"
#include "apptraverse/ui/ui_window_presenter.h"

namespace {

std::filesystem::path DefaultStateDir() {
  return std::filesystem::path{"minimal_app_s1_controls_state"};
}

}  // namespace

int main(int argc, char** argv) {
  apptraverse::EnableNoninteractiveCrt();

  std::filesystem::path state_dir = DefaultStateDir();
#ifdef APPTRAVERSE_MINIMAL_LINUX_TEST_HOOKS
  apptraverse::example::minimal::LinuxMinimalSessionAutomation automation =
      apptraverse::example::minimal::LinuxMinimalSessionAutomation::None;
#endif
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    }
#ifdef APPTRAVERSE_MINIMAL_LINUX_TEST_HOOKS
    else if (arg == "--session-automation" && i + 1 < argc) {
      automation =
          apptraverse::example::minimal::ParseLinuxMinimalSessionAutomation(
              argv[++i]);
    }
#endif
  }

  apptraverse::ApplicationModelRuntime runtime;
  int model_exit_status = 1;

  apptraverse::linux::LinuxApplicationHostCallbacks callbacks;
#ifdef APPTRAVERSE_MINIMAL_LINUX_TEST_HOOKS
  auto automation_hooks =
      apptraverse::example::minimal::MakeLinuxMinimalSessionAutomationHooks(
          automation, [&] { runtime.RequestStop(); }, &runtime);
#endif
  callbacks.run_model = [&](apptraverse::ApplicationPublishedCallback on_published) {
    model_exit_status = apptraverse::example::minimal::RunMinimalApplicationModel(
        state_dir, runtime, on_published);
    if (model_exit_status == 0) {
#ifdef APPTRAVERSE_MINIMAL_LINUX_TEST_HOOKS
      std::cerr << "MINIMAL_APP_SHUTDOWN_PERSIST_OK\n";
#endif
    } else {
      std::cerr << "MINIMAL_APP_SHUTDOWN_PERSIST_FAILED\n";
    }
#ifdef APPTRAVERSE_MINIMAL_LINUX_TEST_HOOKS
    std::cerr << "MINIMAL_APP_LINUX_MODEL_THREAD_EXIT\n";
#endif
  };
  callbacks.request_stop = [&] { runtime.RequestStop(); };
  callbacks.on_desktop_window_close =
      [&](ae::Obj& root, ae::Obj& closed_window,
          apptraverse::ModelObjectProxy& proxy) {
        apptraverse::example::minimal::HandleMinimalAppDesktopWindowClose(
            root, closed_window, proxy, [&] { runtime.RequestStop(); });
      };
  callbacks.after_initial = [&](ae::Obj& root) {
    auto& application = static_cast<apptraverse::example::minimal::MinimalApplication&>(root);
    application.main_window->presenter->model_proxy->Invoke(root.obj_id,
        &apptraverse::example::minimal::MinimalApplication::SubmitDesktopEnvironment,
        static_cast<std::int32_t>(160));
#ifdef APPTRAVERSE_MINIMAL_LINUX_TEST_HOOKS
    automation_hooks.OnAfterInitial(root);
#endif
  };
#ifdef APPTRAVERSE_MINIMAL_LINUX_TEST_HOOKS
  callbacks.after_incremental = [&](ae::Obj& root) {
    automation_hooks.OnAfterIncremental(root);
  };
#endif

  apptraverse::linux::LinuxApplicationHost host(runtime, std::move(callbacks));
  host.Run();
#ifdef APPTRAVERSE_MINIMAL_LINUX_TEST_HOOKS
  if (model_exit_status == 0) {
    std::cerr << "MINIMAL_APP_SHUTDOWN_STOPPED\n";
  }
#endif
  return model_exit_status;
}
