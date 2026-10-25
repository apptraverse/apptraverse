#include "window_catalog_win_app.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/windows/win_application_host.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_window_presenter.h"

namespace apptraverse {
namespace {

ui::windows::WinDesktopWindowPresenter& FindChildWindowPresenter(
    WindowCatalog& catalog, ae::ObjId window_id) {
  for (auto const& row : catalog.list->rows) {
    auto* holder = static_cast<WindowHolder*>(&*row);
    if (!holder->window.is_loaded()) {
      continue;
    }
    if (holder->window.id() != window_id) {
      continue;
    }
    auto& presented =
        static_cast<ui::UiPresentedWindow&>(*holder->window.operator->());
    return static_cast<ui::windows::WinDesktopWindowPresenter&>(
        *presented.presenter);
  }
  WriteFatalStderr("fatal: pending focus window not found in UI catalog\n");
  std::abort();
}

}  // namespace

int WindowCatalogWinApp::Run(std::filesystem::path const& state_dir) {
  session_.state_dir = state_dir;

  ae::ObjId pending_focus{};

  windows::WinApplicationHostCallbacks callbacks;
  callbacks.run_model = [this](ApplicationPublishedCallback on_published) {
    session_.Run([&on_published](WindowCatalogPublicationKind kind) {
      on_published(kind == WindowCatalogPublicationKind::Initial
                       ? ApplicationPublicationKind::Initial
                       : ApplicationPublicationKind::Incremental);
    });
  };
  callbacks.request_stop = [this] { session_.RequestStop(); };
  callbacks.on_desktop_window_close =
      [this](ae::Obj& application_root, ae::Obj& closed_window,
             ModelObjectProxy& proxy) {
        auto& catalog = static_cast<WindowCatalog&>(application_root);
        if (catalog.main_window.id() == closed_window.obj_id) {
          session_.RequestStop();
          return;
        }
        proxy.Invoke(catalog.obj_id, &WindowCatalog::UserCloseWindow,
                     closed_window.obj_id);
      };
  callbacks.take_incremental_sidecar = [this, &pending_focus] {
    pending_focus = session_.pending_focus_for_ui;
    session_.pending_focus_for_ui = {};
  };
  callbacks.after_incremental = [&pending_focus](ae::Obj& application_root) {
    if (!pending_focus.is_valid()) {
      return;
    }
    auto& catalog = static_cast<WindowCatalog&>(application_root);
    ui::windows::WinDesktopWindowPresenter& presenter =
        FindChildWindowPresenter(catalog, pending_focus);
    HWND const hwnd = presenter.hwnd();
    if (IsIconic(hwnd)) {
      ShowWindow(hwnd, SW_RESTORE);
    }
    SetForegroundWindow(hwnd);
    pending_focus = {};
  };

  windows::WinApplicationLoadingOptions loading{
      L"AppTraverseWindowCatalogLoading", L"Loading catalog", L"Loading catalog",
      200, 200, 320, 120};

  windows::WinApplicationHost host(session_.runtime, loading,
                                   std::move(callbacks));
  host.Run();
  return 0;
}

}  // namespace apptraverse
