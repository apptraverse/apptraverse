#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#undef RegisterClass
#endif

#include <filesystem>
#include <string_view>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/windows/win_application_host.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_window_presenter.h"

#include "window_catalog_lifecycle.h"
#include "window_catalog_model.h"

namespace {

std::filesystem::path DefaultStateDir() {
  wchar_t module_path[MAX_PATH]{};
  DWORD const len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return std::filesystem::path{"surfaces_catalog_v0_state"};
  }
  return std::filesystem::path{module_path}.parent_path() /
         "surfaces_catalog_v0_state";
}

apptraverse::ui::windows::WinDesktopWindowPresenter& FindChildWindowPresenter(
    apptraverse::WindowCatalog& catalog, ae::ObjId window_id) {
  for (auto const& row : catalog.list->rows) {
    auto* holder = static_cast<apptraverse::WindowHolder*>(&*row);
    if (!holder->window.is_loaded()) {
      continue;
    }
    if (holder->window.id() != window_id) {
      continue;
    }
    auto& presented = static_cast<apptraverse::ui::UiPresentedWindow&>(
        *holder->window.operator->());
    return static_cast<apptraverse::ui::windows::WinDesktopWindowPresenter&>(
        *presented.presenter);
  }
  apptraverse::WriteFatalStderr(
      "fatal: pending focus window not found in UI catalog\n");
  std::abort();
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

  apptraverse::WindowCatalogModelSession session;
  session.state_dir = state_dir;

  ae::ObjId pending_focus{};

  apptraverse::windows::WinApplicationHostCallbacks callbacks;
  callbacks.run_model =
      [&session](apptraverse::ApplicationPublishedCallback on_published) {
        session.Run(std::move(on_published));
      };
  callbacks.request_stop = [&] { session.runtime.RequestStop(); };
  callbacks.on_desktop_window_close =
      [&session](ae::Obj& application_root, ae::Obj& closed_window,
                 apptraverse::ModelObjectProxy& proxy) {
        auto& catalog = static_cast<apptraverse::WindowCatalog&>(application_root);
        if (catalog.main_window.id() == closed_window.obj_id) {
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
  callbacks.after_incremental = [&pending_focus](ae::Obj& application_root) {
    if (!pending_focus.is_valid()) {
      return;
    }
    auto& catalog =
        static_cast<apptraverse::WindowCatalog&>(application_root);
    apptraverse::ui::windows::WinDesktopWindowPresenter& presenter =
        FindChildWindowPresenter(catalog, pending_focus);
    HWND const hwnd = presenter.hwnd();
    if (IsIconic(hwnd)) {
      ShowWindow(hwnd, SW_RESTORE);
    }
    SetForegroundWindow(hwnd);
    pending_focus = {};
  };

  apptraverse::windows::WinApplicationLoadingOptions loading{
      L"AppTraverseWindowCatalogLoading", L"Loading catalog", L"Loading catalog",
      200, 200, 320, 120};

  apptraverse::windows::WinApplicationHost host(session.runtime, loading,
                                              std::move(callbacks));
  host.Run();
  return 0;
}
