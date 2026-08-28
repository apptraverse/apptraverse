#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "aether/clock.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

#include "demo_bootstrap.h"
#include "demo_model.h"
#include "win_app.h"
#include "win_presenters.h"

namespace {

void DistillDemo(std::filesystem::path const& dir) {
  apptraverse::EnsureDemoRegistration();
  apptraverse::EnsureWindowsPresenterRegistration();
  std::filesystem::remove_all(dir);
  apptraverse::DirectoryDomainStorage storage{dir};
  ae::Domain domain{ae::Now(), storage};
  auto application = apptraverse::BuildDemoGraph(domain);
  apptraverse::FinalizeDistilledGraph(*application);
  apptraverse::SaveDistilledRoot(*application);  // runtime-save-ok: distill
  auto presentation =
      apptraverse::BuildPresentationGraph(domain, *application);
  apptraverse::SaveDistilledRoot(*presentation);  // runtime-save-ok: distill
}

}  // namespace

int main(int argc, char** argv) {
  bool distill = false;
  std::filesystem::path state_dir{"model_ui_runtime_state"};
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--distill") {
      distill = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        state_dir = argv[++i];
      }
    } else if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    }
  }

  apptraverse::EnsureDemoRegistration();
  apptraverse::EnsureWindowsPresenterRegistration();
  if (distill) {
    DistillDemo(state_dir);
    return 0;
  }
  if (!std::filesystem::exists(state_dir)) {
    std::cerr << "distilled state missing: " << state_dir.string()
              << "\nrun with --distill <dir>\n";
    return 1;
  }
  apptraverse::WinApp app;
  return app.Run(state_dir);
}
