#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include <gtk/gtk.h>

#include "apptraverse/noninteractive_crt.h"
#include "window_catalog_graph.h"
#include "window_catalog_linux_application.h"
#include "window_catalog_linux_session_automation.h"

namespace {

void Check(bool condition, char const* message) {
  if (!condition) {
    std::cerr << "CHECK failed: " << message << '\n';
    std::exit(2);
  }
}

struct UserFileSnapshot {
  bool exists = false;
  std::filesystem::file_time_type last_write{};
  std::string bytes;

  bool operator==(UserFileSnapshot const&) const = default;
};

UserFileSnapshot SnapshotUserFile(std::filesystem::path const& profile) {
  UserFileSnapshot snapshot;
  auto const path = profile / "user.bin";
  snapshot.exists = std::filesystem::exists(path);
  if (snapshot.exists) {
    snapshot.last_write = std::filesystem::last_write_time(path);
    std::ifstream input{path, std::ios::binary};
    Check(static_cast<bool>(input), "read current USER");
    snapshot.bytes.assign(std::istreambuf_iterator<char>{input},
                          std::istreambuf_iterator<char>{});
    Check(!input.bad(), "read complete USER");
  }
  return snapshot;
}

gboolean FailOnTimeout(gpointer) {
  std::cerr << "CHECK failed: Linux acceptance phase timed out\n";
  std::exit(3);
}

}  // namespace

int main(int argc, char** argv) {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureWindowCatalogGraphRegistration();

  std::filesystem::path state_dir;
  using apptraverse::example::surfaces::LinuxWindowCatalogSessionAutomation;
  auto mode = LinuxWindowCatalogSessionAutomation::None;
  for (int i = 1; i < argc; ++i) {
    std::string_view const arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--session-automation" && i + 1 < argc) {
      mode = apptraverse::example::surfaces::
          ParseLinuxWindowCatalogSessionAutomation(argv[++i]);
    } else {
      Check(false, "unknown or incomplete acceptance argument");
    }
  }
  Check(!state_dir.empty(), "explicit test profile required");
  Check(mode != LinuxWindowCatalogSessionAutomation::None,
        "explicit acceptance phase required");

  apptraverse::WindowCatalogLinuxApplication app{state_dir};
  auto hooks = apptraverse::example::surfaces::
      MakeLinuxWindowCatalogSessionAutomationHooks(
          mode, [&] { app.session().runtime.RequestStop(); },
          &app.session().runtime);
  auto callbacks = app.MakeHostCallbacks();
#if !defined(APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
  auto const before_bootstrap = SnapshotUserFile(state_dir);
#endif
  UserFileSnapshot at_start;
  auto after_initial = std::move(callbacks.after_initial);
  callbacks.after_initial = [&, after_initial = std::move(after_initial)](
                                ae::Obj& root) {
    after_initial(root);
    // Debug bootstrap has now selected its fresh debug-run-*/user directory.
    at_start = SnapshotUserFile(app.session().state_dir);
#if defined(APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
    Check(!at_start.exists, "fresh Debug did not write USER at startup");
    std::cout << "WINDOW_CATALOG_DEBUG_FRESH_PROFILE="
              << app.session().state_dir.string() << '\n';
#else
    Check(at_start == before_bootstrap, "USER changed during application startup");
#endif
    hooks.OnAfterInitial(root);
  };
  auto after_incremental = std::move(callbacks.after_incremental);
  callbacks.after_incremental =
      [&, after_incremental = std::move(after_incremental)](ae::Obj& root) {
        after_incremental(root);
        Check(SnapshotUserFile(app.session().state_dir) == at_start,
              "USER changed before application shutdown");
        hooks.OnAfterIncremental(root);
      };

  guint const timeout = g_timeout_add_seconds(180, &FailOnTimeout, nullptr);
  int const result = app.Run(std::move(callbacks));
  g_source_remove(timeout);
  Check(result == 0, "model session/shutdown succeeded");
  auto const saved = SnapshotUserFile(app.session().state_dir);
  Check(saved.exists && !saved.bytes.empty(), "shutdown wrote user.bin");
  std::size_t file_count = 0;
  for (auto const& entry :
       std::filesystem::recursive_directory_iterator(app.session().state_dir)) {
    if (entry.is_regular_file()) ++file_count;
  }
  Check(file_count == 1, "USER contains exactly one file");
  std::cerr << "WINDOW_CATALOG_LINUX_USER_FILE_OK\n";
  return result;
}
