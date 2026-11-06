#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

#include "window_catalog_lifecycle.h"
#include "window_catalog_linux_desktop_zorder.h"
#include "window_catalog_linux_session_automation.h"
#include "window_catalog_model.h"

#include <gtk/gtk.h>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/linux/linux_application_host.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "apptraverse/ui/ui_presented_window.h"

namespace {

std::filesystem::path DefaultStateDir() {
  return std::filesystem::path{"window_catalog_s1_linux_state"};
}

apptraverse::ui::linux::LinuxDesktopWindowPresenter& FindChildWindowPresenter(
    apptraverse::WindowCatalog& catalog, ae::ObjId window_id) {
  catalog.persisted.list.Load();
  for (auto const& row : catalog.persisted.list->rows) {
    auto* holder = static_cast<apptraverse::WindowHolder*>(&*row);
    if (!holder->window.is_loaded()) {
      continue;
    }
    if (holder->window.id() != window_id) {
      continue;
    }
    auto& presented = static_cast<apptraverse::ui::UiPresentedWindow&>(
        *holder->window.operator->());
    return static_cast<apptraverse::ui::linux::LinuxDesktopWindowPresenter&>(
        *presented.presenter);
  }
  apptraverse::WriteFatalStderr(
      "fatal: pending focus window not found in UI catalog\n");
  std::abort();
}

}  // namespace

int main(int argc, char** argv) {
  apptraverse::EnableNoninteractiveCrt();

  std::filesystem::path state_dir = DefaultStateDir();
  apptraverse::example::surfaces::LinuxWindowCatalogSessionAutomation automation =
      apptraverse::example::surfaces::LinuxWindowCatalogSessionAutomation::None;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--session-automation" && i + 1 < argc) {
      automation =
          apptraverse::example::surfaces::ParseLinuxWindowCatalogSessionAutomation(
              argv[++i]);
    }
  }

  apptraverse::WindowCatalogModelSession session;
  session.state_dir = state_dir;

  ae::ObjId pending_focus{};
  int model_exit_status = 1;

  apptraverse::linux::LinuxApplicationHostCallbacks callbacks;
  auto automation_hooks =
      apptraverse::example::surfaces::MakeLinuxWindowCatalogSessionAutomationHooks(
          automation, [&] { session.runtime.RequestStop(); }, &session.runtime);
  callbacks.run_model =
      [&session, &model_exit_status](
          apptraverse::ApplicationPublishedCallback on_published) {
        model_exit_status = session.Run(std::move(on_published));
        if (model_exit_status == 0) {
          std::cerr << "WINDOW_CATALOG_SHUTDOWN_PERSIST_OK\n";
        } else {
          std::cerr << "WINDOW_CATALOG_SHUTDOWN_PERSIST_FAILED\n";
        }
        std::cerr << "WINDOW_CATALOG_LINUX_MODEL_THREAD_EXIT\n";
      };
  callbacks.request_stop = [&] { session.runtime.RequestStop(); };
  callbacks.on_desktop_window_close =
      [&session](ae::Obj& application_root, ae::Obj& closed_window,
                 apptraverse::ModelObjectProxy& proxy) {
        auto& catalog =
            static_cast<apptraverse::WindowCatalog&>(application_root);
        if (catalog.persisted.main_window.id() == closed_window.obj_id) {
          apptraverse::example::surfaces::FlushCatalogNativeFrames(catalog);
          std::vector<std::uint32_t> const z_order =
              apptraverse::example::surfaces::
                  CaptureCatalogDesktopZOrderTopToBottom(catalog);
          if (!z_order.empty()) {
            proxy.Invoke(catalog.obj_id,
                         &apptraverse::WindowCatalog::SetDesktopZOrderFromObjIds,
                         z_order);
          }
          session.runtime.RequestStop();
          return;
        }
        proxy.Invoke(catalog.obj_id,
                     &apptraverse::WindowCatalog::UserCloseWindow,
                     closed_window.obj_id);
      };
  callbacks.take_incremental_sidecar = [&session, &pending_focus] {
    pending_focus = session.pending_focus_for_ui;
    session.pending_focus_for_ui = {};
  };
  callbacks.after_incremental = [&pending_focus, &automation_hooks](
                                    ae::Obj& application_root) {
    automation_hooks.OnAfterIncremental(application_root);
    if (!pending_focus.is_valid()) {
      return;
    }
    auto& catalog =
        static_cast<apptraverse::WindowCatalog&>(application_root);
    apptraverse::ui::linux::LinuxDesktopWindowPresenter& presenter =
        FindChildWindowPresenter(catalog, pending_focus);
    GtkWidget* widget = presenter.TopLevelWindow();
    if (widget != nullptr) {
      gtk_window_present(GTK_WINDOW(widget));
    }
    pending_focus = {};
  };
  callbacks.after_initial = [&automation_hooks](ae::Obj& application_root) {
    auto& catalog =
        static_cast<apptraverse::WindowCatalog&>(application_root);
    apptraverse::example::surfaces::RestoreCatalogDesktopZOrder(catalog);
    automation_hooks.OnAfterInitial(application_root);
  };

  apptraverse::linux::LinuxApplicationHost host(session.runtime,
                                                std::move(callbacks));
  host.Run();
  if (model_exit_status == 0) {
    std::cerr << "WINDOW_CATALOG_SHUTDOWN_STOPPED\n";
  }
  return model_exit_status;
}
