#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#undef RegisterClass
#endif

#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

apptraverse::ui::windows::WinDesktopWindowPresenter* CatalogWindowPresenter(
    apptraverse::CatalogWindow& window) {
  auto& presented =
      static_cast<apptraverse::ui::UiPresentedWindow&>(window);
  if (!presented.presenter.is_loaded()) {
    return nullptr;
  }
  return &static_cast<apptraverse::ui::windows::WinDesktopWindowPresenter&>(
      *presented.presenter);
}

void TryMapCatalogWindowHwnd(apptraverse::CatalogWindow::ptr const& window_ref,
                             std::unordered_map<HWND, ae::ObjId::Type>& out) {
  if (!window_ref.is_valid() || !window_ref.is_loaded()) {
    return;
  }
  apptraverse::ui::windows::WinDesktopWindowPresenter* presenter =
      CatalogWindowPresenter(*window_ref);
  if (presenter == nullptr) {
    return;
  }
  HWND const hwnd = presenter->hwnd();
  if (hwnd == nullptr) {
    return;
  }
  out[hwnd] = window_ref.id().id();
}

std::unordered_map<HWND, ae::ObjId::Type> BuildCatalogWindowHwndMap(
    apptraverse::WindowCatalog& catalog) {
  std::unordered_map<HWND, ae::ObjId::Type> map;
  TryMapCatalogWindowHwnd(catalog.main_window, map);
  if (catalog.list) {
    for (auto const& row : catalog.list->rows) {
      if (!row) {
        continue;
      }
      auto* holder = static_cast<apptraverse::WindowHolder*>(&*row);
      TryMapCatalogWindowHwnd(holder->window, map);
    }
  }
  return map;
}

std::vector<std::uint32_t> CaptureCatalogDesktopZOrderTopToBottom(
    apptraverse::WindowCatalog& catalog) {
  std::unordered_map<HWND, ae::ObjId::Type> const hwnd_map =
      BuildCatalogWindowHwndMap(catalog);
  if (hwnd_map.empty()) {
    return {};
  }
  struct Ctx {
    std::unordered_map<HWND, ae::ObjId::Type> const* map;
    std::vector<std::uint32_t>* out;
  };
  Ctx ctx{&hwnd_map, {} };
  std::vector<std::uint32_t> ordered;
  ctx.out = &ordered;
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        auto const it = c->map->find(hwnd);
        if (it == c->map->end()) {
          return TRUE;
        }
        c->out->push_back(it->second);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ordered;
}

void ApplyDesktopWindowsZOrderTopToBottom(std::vector<HWND> const& top_to_bottom) {
  for (auto it = top_to_bottom.rbegin(); it != top_to_bottom.rend(); ++it) {
    if (*it == nullptr) {
      continue;
    }
    if (SetWindowPos(*it, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) == 0) {
      apptraverse::WriteFatalStderr(
          "fatal: SetWindowPos desktop z-order restore failed\n");
      std::abort();
    }
  }
}

std::vector<HWND> HwndsForCatalogDesktopZOrder(
    apptraverse::WindowCatalog& catalog) {
  std::unordered_map<ae::ObjId::Type, HWND> id_to_hwnd;
  for (auto const& [hwnd, id] : BuildCatalogWindowHwndMap(catalog)) {
    id_to_hwnd[id] = hwnd;
  }
  std::vector<HWND> ordered;
  std::unordered_set<HWND> placed;
  for (auto const& window_ref : catalog.desktop_z_order) {
    if (!window_ref.is_valid()) {
      continue;
    }
    auto const it = id_to_hwnd.find(window_ref.id().id());
    if (it == id_to_hwnd.end()) {
      continue;
    }
    HWND const hwnd = it->second;
    if (placed.insert(hwnd).second) {
      ordered.push_back(hwnd);
    }
  }
  for (auto const& [hwnd, id] : BuildCatalogWindowHwndMap(catalog)) {
    (void)id;
    if (placed.insert(hwnd).second) {
      ordered.push_back(hwnd);
    }
  }
  return ordered;
}

void RestoreCatalogDesktopZOrder(apptraverse::WindowCatalog& catalog) {
  if (catalog.desktop_z_order.empty()) {
    return;
  }
  ApplyDesktopWindowsZOrderTopToBottom(HwndsForCatalogDesktopZOrder(catalog));
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
  int model_exit_status = 1;

  apptraverse::windows::WinApplicationHostCallbacks callbacks;
  callbacks.run_model =
      [&session, &model_exit_status](
          apptraverse::ApplicationPublishedCallback on_published) {
        model_exit_status = session.Run(std::move(on_published));
      };
  callbacks.request_stop = [&] { session.runtime.RequestStop(); };
  callbacks.on_desktop_window_close =
      [&session](ae::Obj& application_root, ae::Obj& closed_window,
                 apptraverse::ModelObjectProxy& proxy) {
        auto& catalog = static_cast<apptraverse::WindowCatalog&>(application_root);
        if (catalog.main_window.id() == closed_window.obj_id) {
          std::vector<std::uint32_t> const z_order =
              CaptureCatalogDesktopZOrderTopToBottom(catalog);
          proxy.Invoke(catalog.obj_id,
                       &apptraverse::WindowCatalog::SetDesktopZOrderFromObjIds,
                       z_order);
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
  callbacks.after_initial = [](ae::Obj& application_root) {
    auto& catalog =
        static_cast<apptraverse::WindowCatalog&>(application_root);
    RestoreCatalogDesktopZOrder(catalog);
  };

  apptraverse::windows::WinApplicationLoadingOptions loading{
      L"AppTraverseWindowCatalogLoading", L"Loading catalog", L"Loading catalog",
      200, 200, 320, 120};

  apptraverse::windows::WinApplicationHost host(session.runtime, loading,
                                              std::move(callbacks));
  host.Run();
  return model_exit_status;
}
