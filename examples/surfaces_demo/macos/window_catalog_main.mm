#import <AppKit/AppKit.h>

#include <filesystem>
#include <string>
#include <vector>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/macos/mac_application_host.h"

#include "window_catalog_lifecycle.h"
#include "window_catalog_model.h"

namespace {

std::filesystem::path DefaultStateDir() {
  NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(
      NSApplicationSupportDirectory, NSUserDomainMask, YES);
  NSString* base = paths.firstObject;
  if (base == nil) {
    return "AppTraverseWindowCatalogExperiment";
  }
  return std::filesystem::path{base.UTF8String} /
         "AppTraverseWindowCatalogExperiment";
}

std::filesystem::path StateDirFromProcess() {
  std::filesystem::path state_dir = DefaultStateDir();
  NSArray<NSString*>* args = NSProcessInfo.processInfo.arguments;
  for (NSUInteger i = 0; i < args.count; ++i) {
    if ([args[i] isEqualToString:@"--state-dir"] && i + 1 < args.count) {
      state_dir = args[i + 1].UTF8String;
    }
  }
  std::error_code created;
  std::filesystem::create_directories(state_dir, created);
  if (created) {
    apptraverse::WriteFatalStderr("fatal: cannot create catalog profile directory\n");
    std::abort();
  }
  return state_dir;
}

std::vector<std::uint32_t> OpenCatalogWindowIds(
    apptraverse::WindowCatalog& catalog) {
  std::vector<std::uint32_t> ids;
  if (catalog.main_window) {
    ids.push_back(static_cast<std::uint32_t>(catalog.main_window.id().id()));
  }
  if (!catalog.list) {
    return ids;
  }
  for (auto const& row : catalog.list->rows) {
    if (!row) {
      continue;
    }
    auto* holder = static_cast<apptraverse::WindowHolder*>(&*row);
    if (!holder->window.is_loaded()) {
      continue;
    }
    ids.push_back(static_cast<std::uint32_t>(holder->window.id().id()));
  }
  return ids;
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  @autoreleasepool {
    apptraverse::EnableNoninteractiveCrt();
    apptraverse::WindowCatalogModelSession session;
    session.state_dir = StateDirFromProcess();

    ae::ObjId pending_focus{};
    int model_exit_status = 1;
    apptraverse::macos::MacApplicationHost* host_ptr = nullptr;

    apptraverse::macos::MacApplicationHostCallbacks callbacks;
    callbacks.run_model =
        [&session, &model_exit_status](
            apptraverse::ApplicationPublishedCallback on_published) {
          model_exit_status = session.Run(std::move(on_published));
        };
    callbacks.request_stop = [&session] { session.runtime.RequestStop(); };
    callbacks.request_persist = [&session](std::function<void(int)> done) {
      if (!session.request_persist) {
        done(1);
        return;
      }
      session.request_persist(std::move(done));
    };
    callbacks.on_desktop_window_close =
        [&session](ae::Obj& application_root, ae::Obj& closed_window,
                   apptraverse::ModelObjectProxy& proxy) {
          auto& catalog =
              static_cast<apptraverse::WindowCatalog&>(application_root);
          if (catalog.main_window.id() == closed_window.obj_id) {
            proxy.Invoke(catalog.obj_id,
                         &apptraverse::WindowCatalog::SetDesktopZOrderFromObjIds,
                         OpenCatalogWindowIds(catalog));
            session.runtime.RequestStop();
            return;
          }
          proxy.Invoke(catalog.obj_id, &apptraverse::WindowCatalog::UserCloseWindow,
                       closed_window.obj_id);
        };
    callbacks.take_incremental_sidecar = [&session, &pending_focus] {
      pending_focus = session.pending_focus_for_ui;
      session.pending_focus_for_ui = {};
    };
    callbacks.after_incremental = [&host_ptr, &pending_focus](ae::Obj&) {
      if (host_ptr == nullptr || !pending_focus.is_valid()) {
        return;
      }
      host_ptr->FocusWindow(static_cast<std::uint64_t>(pending_focus.id()));
      pending_focus = {};
    };

    apptraverse::macos::MacApplicationHost host(session.runtime,
                                               std::move(callbacks));
    host_ptr = &host;
    host.Run();
    return model_exit_status;
  }
}
