#include <filesystem>
#include <string_view>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/linux/linux_application_host.h"

#include "minimal_app_lifecycle.h"

namespace {

std::filesystem::path DefaultStateDir() {
  return std::filesystem::path{"minimal_app_s1_controls_state"};
}

}  // namespace

int main(int argc, char** argv) {
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

  apptraverse::linux::LinuxApplicationHostCallbacks callbacks;
  callbacks.run_model = [&](apptraverse::ApplicationPublishedCallback on_published) {
    model_exit_status = apptraverse::example::minimal::RunMinimalApplicationModel(
        state_dir, runtime, on_published, true);
  };
  callbacks.request_stop = [&] { runtime.RequestStop(); };
  callbacks.on_desktop_window_close = [&](ae::Obj&, ae::Obj&,
                                          apptraverse::ModelObjectProxy&) {
    runtime.RequestStop();
  };

  apptraverse::linux::LinuxApplicationHost host(runtime, std::move(callbacks));
  host.Run();
  return model_exit_status;
}
