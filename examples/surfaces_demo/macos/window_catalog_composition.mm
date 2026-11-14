#import "window_catalog_composition.h"

#include <filesystem>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "apptraverse/platform/macos/macos_application_bridge.h"
#include "apptraverse/platform/macos/macos_ui_presenters.h"
#include "apptraverse/noninteractive_crt.h"
#include "window_catalog_graph.h"
#include "window_catalog_lifecycle.h"
#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_model.h"
#if defined(APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
#include "window_catalog_distillation.h"
#endif

namespace {
std::filesystem::path NativeDirectory(NSString* directory) {
  char const* native_path = directory.fileSystemRepresentation;
  if (!native_path || native_path[0] == '\0') {
    apptraverse::WriteFatalStderr("cannot resolve WindowCatalog directory\n");
    std::abort();
  }
  return native_path;
}
struct CatalogApplication {
  apptraverse::WindowCatalogModelSession session;
  ae::ObjId pending_focus{};
};
}  // namespace

void ATRegisterWindowCatalogApplication(void) {
  apptraverse::EnsureWindowCatalogGraphRegistration();
}

BOOL ATDistillWindowCatalogApplication(NSString* directory) {
#if defined(APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
  apptraverse::DistillWindowCatalog(
      std::filesystem::absolute(NativeDirectory(directory)));
  return YES;
#else
  return NO;
#endif
}

ATMacApplicationBridge* ATCreateWindowCatalogApplicationBridge(
    NSString* userDirectory, id<ATMacApplicationBridgeDelegate> delegate) {
  auto application = std::make_shared<CatalogApplication>();
  application->session.state_dir = NativeDirectory(userDirectory);
  apptraverse::ui::macos::MacApplicationBridgeCallbacks callbacks;
  callbacks.runtime = &application->session.runtime;
  callbacks.run_model =
      [application](apptraverse::ApplicationModelRuntime&,
                    apptraverse::ApplicationPublishedCallback on_published) {
        return application->session.Run(std::move(on_published));
      };
  callbacks.prepare_ui_presenters = [application](ae::Obj& ui_root) {
    apptraverse::WireWindowCatalogMainNativeFrameQueue(
        ui_root, application->session.main_native_frame_queue);
  };
  callbacks.on_application_quit =
      [](ae::Obj& root, apptraverse::ModelObjectProxy& proxy,
         std::function<void()> request_stop) {
        auto& catalog = static_cast<apptraverse::WindowCatalog&>(root);
        proxy.Invoke(catalog.obj_id,
                     &apptraverse::WindowCatalog::SetWindowOrderFromObjIds,
                     apptraverse::ui::macos::CaptureMacDesktopZOrder(root));
        request_stop();
      };
  callbacks.close_desktop_window =
      [](ae::Obj& root, ae::Obj& closed_window,
                    apptraverse::ModelObjectProxy& proxy,
                    std::function<void()> request_stop) {
        auto& catalog = static_cast<apptraverse::WindowCatalog&>(root);
        bool const closes_application =
            catalog.persisted.main_window.id() == closed_window.obj_id;
        if (closes_application) {
          proxy.Invoke(catalog.obj_id,
                       &apptraverse::WindowCatalog::SetWindowOrderFromObjIds,
                       apptraverse::ui::macos::CaptureMacDesktopZOrder(root));
          request_stop();
        } else {
          proxy.Invoke(catalog.obj_id,
                       &apptraverse::WindowCatalog::UserCloseWindow,
                       closed_window.obj_id);
        }
        return closes_application;
      };
  callbacks.take_incremental_sidecar = [application] {
    application->pending_focus = application->session.pending_focus_for_ui;
    application->session.pending_focus_for_ui = {};
  };
  callbacks.after_incremental = [application](ae::Obj& root) {
    if (application->pending_focus.is_valid()) {
      apptraverse::ui::macos::FocusMacDesktopWindow(root,
                                                  application->pending_focus);
      application->pending_focus = {};
    }
  };
  callbacks.after_initial = [](ae::Obj& root) {
    auto& catalog = static_cast<apptraverse::WindowCatalog&>(root);
    std::vector<std::uint32_t> ids;
    for (auto const& window : catalog.persisted.window_order) {
      ids.push_back(window.id().id());
    }
    apptraverse::ui::macos::RestoreMacDesktopZOrder(root, ids);
    apptraverse::ui::macos::FocusMacDesktopWindow(
        root, catalog.ResolveActiveWindow().id());
  };
  return apptraverse::ui::macos::MakeMacApplicationBridge(
      std::move(callbacks), delegate);
}
