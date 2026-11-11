#include "window_catalog_linux_application.h"

#include <cstdlib>
#include <utility>

#include "window_catalog_linux_desktop_zorder.h"
#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_model.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"

namespace apptraverse {
namespace {

ui::linux::LinuxDesktopWindowPresenter& FindChildWindowPresenter(
    WindowCatalog& catalog, ae::ObjId window_id) {
  catalog.persisted.list.Load();
  for (auto const& row : catalog.persisted.list->rows) {
    row.Load();
    auto& holder = static_cast<WindowHolder&>(*row);
    if (!holder.window.is_loaded() || holder.window.id() != window_id) {
      continue;
    }
    return static_cast<ui::linux::LinuxDesktopWindowPresenter&>(
        *holder.window->presenter);
  }
  WriteFatalStderr("fatal: pending focus window not found in UI catalog\n");
  std::abort();
}

}  // namespace

WindowCatalogLinuxApplication::WindowCatalogLinuxApplication(
    std::filesystem::path state_directory) {
  session_.state_dir = std::move(state_directory);
}

linux::LinuxApplicationHostCallbacks
WindowCatalogLinuxApplication::MakeHostCallbacks() {
  linux::LinuxApplicationHostCallbacks callbacks;
  callbacks.run_model = [this](ApplicationPublishedCallback on_published) {
    model_exit_status_ = session_.Run(std::move(on_published));
  };
  callbacks.request_stop = [this] { session_.runtime.RequestStop(); };
  callbacks.on_desktop_window_close =
      [this](ae::Obj& application_root, ae::Obj& closed_window,
             ModelObjectProxy& proxy) {
        auto& catalog = static_cast<WindowCatalog&>(application_root);
        if (catalog.persisted.main_window.id() == closed_window.obj_id) {
          example::surfaces::FlushCatalogNativeFrames(catalog);
          auto const z_order =
              example::surfaces::CaptureCatalogDesktopZOrderTopToBottom(catalog);
          if (!z_order.empty()) {
            proxy.Invoke(catalog.obj_id, &WindowCatalog::SetDesktopZOrderFromObjIds,
                         z_order);
          }
          session_.runtime.RequestStop();
          return;
        }
        proxy.Invoke(catalog.obj_id, &WindowCatalog::UserCloseWindow,
                     closed_window.obj_id);
      };
  callbacks.take_incremental_sidecar = [this] {
    pending_focus_ = session_.pending_focus_for_ui;
    session_.pending_focus_for_ui = {};
  };
  callbacks.after_incremental = [this](ae::Obj& application_root) {
    if (!pending_focus_.is_valid()) { return; }
    auto& catalog = static_cast<WindowCatalog&>(application_root);
    auto& presenter = FindChildWindowPresenter(catalog, pending_focus_);
    gtk_window_present(GTK_WINDOW(presenter.TopLevelWindow()));
    pending_focus_ = {};
  };
  callbacks.after_initial = [](ae::Obj& application_root) {
    auto& catalog = static_cast<WindowCatalog&>(application_root);
    example::surfaces::RestoreCatalogDesktopZOrder(catalog);
  };
  callbacks.prepare_ui_presenters = [this](ae::Obj& ui_root) {
    WireWindowCatalogMainNativeFrameQueue(ui_root,
                                          session_.main_native_frame_queue);
  };
  return callbacks;
}

int WindowCatalogLinuxApplication::Run(
    linux::LinuxApplicationHostCallbacks callbacks) {
  linux::LinuxApplicationHost host{session_.runtime, std::move(callbacks)};
  host.Run();
  session_.main_native_frame_queue.ClosePosting();
  return model_exit_status_;
}

}  // namespace apptraverse
