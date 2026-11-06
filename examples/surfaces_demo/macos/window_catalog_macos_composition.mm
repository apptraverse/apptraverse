#import "window_catalog_macos_composition.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/macos/macos_application_bridge.h"
#include "apptraverse/platform/macos/macos_ui_presenters.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "window_catalog_lifecycle_macos.h"
#include "window_catalog_model.h"

namespace {

using apptraverse::CatalogWindow;
using apptraverse::WindowCatalog;
using apptraverse::ui::macos::MacDesktopWindowPresenter;

MacDesktopWindowPresenter* CatalogWindowPresenter(CatalogWindow& window) {
  auto& presented =
      static_cast<apptraverse::ui::UiPresentedWindow&>(window);
  if (!presented.presenter.is_loaded()) {
    return nullptr;
  }
  return &static_cast<MacDesktopWindowPresenter&>(*presented.presenter);
}

uint64_t PresenterToken(MacDesktopWindowPresenter* presenter) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(presenter));
}

void TryMapCatalogWindowToken(CatalogWindow::ptr const& window_ref,
                              std::unordered_map<uint64_t, ae::ObjId::Type>& out) {
  if (!window_ref.is_valid() || !window_ref.is_loaded()) {
    return;
  }
  MacDesktopWindowPresenter* presenter = CatalogWindowPresenter(*window_ref);
  if (presenter == nullptr) {
    return;
  }
  out[PresenterToken(presenter)] = window_ref.id().id();
}

std::unordered_map<uint64_t, ae::ObjId::Type> BuildCatalogWindowTokenMap(
    WindowCatalog& catalog) {
  std::unordered_map<uint64_t, ae::ObjId::Type> map;
  TryMapCatalogWindowToken(catalog.main_window, map);
  if (catalog.list) {
    for (auto const& row : catalog.list->rows) {
      if (!row) {
        continue;
      }
      auto* holder = static_cast<apptraverse::WindowHolder*>(&*row);
      TryMapCatalogWindowToken(holder->window, map);
    }
  }
  return map;
}

std::vector<std::uint32_t> CaptureCatalogDesktopZOrderTopToBottom(
    WindowCatalog& catalog,
    id<ATMacApplicationBridgeDelegate> delegate) {
  std::unordered_map<uint64_t, ae::ObjId::Type> const token_map =
      BuildCatalogWindowTokenMap(catalog);
  if (token_map.empty()) {
    return {};
  }
  NSArray<NSNumber*>* const native_order =
      [delegate bridgeCaptureDesktopPresenterTokensFrontToBack];
  std::vector<std::uint32_t> ordered;
  ordered.reserve(native_order.count);
  for (NSNumber* token in native_order) {
    auto const it = token_map.find(token.unsignedLongLongValue);
    if (it == token_map.end()) {
      continue;
    }
    ordered.push_back(it->second);
  }
  return ordered;
}

std::vector<uint64_t> PresenterTokensForCatalogDesktopZOrder(
    WindowCatalog& catalog) {
  std::unordered_map<ae::ObjId::Type, uint64_t> id_to_token;
  for (auto const& [token, id] : BuildCatalogWindowTokenMap(catalog)) {
    id_to_token[id] = token;
  }
  std::vector<uint64_t> ordered;
  std::unordered_set<uint64_t> placed;
  for (auto const& window_ref : catalog.desktop_z_order) {
    if (!window_ref.is_valid()) {
      continue;
    }
    auto const it = id_to_token.find(window_ref.id().id());
    if (it == id_to_token.end()) {
      continue;
    }
    uint64_t const token = it->second;
    if (placed.insert(token).second) {
      ordered.push_back(token);
    }
  }
  for (auto const& [token, id] : BuildCatalogWindowTokenMap(catalog)) {
    (void)id;
    if (placed.insert(token).second) {
      ordered.push_back(token);
    }
  }
  return ordered;
}

void RestoreCatalogDesktopZOrder(
    WindowCatalog& catalog, id<ATMacApplicationBridgeDelegate> delegate) {
  if (catalog.desktop_z_order.empty()) {
    return;
  }
  std::vector<uint64_t> const tokens =
      PresenterTokensForCatalogDesktopZOrder(catalog);
  if (tokens.empty()) {
    return;
  }
  NSMutableArray<NSNumber*>* ns_tokens =
      [[NSMutableArray alloc] initWithCapacity:tokens.size()];
  for (uint64_t token : tokens) {
    [ns_tokens addObject:@(token)];
  }
  [delegate bridgeRestoreDesktopZOrder:ns_tokens];
}

MacDesktopWindowPresenter& FindChildWindowPresenter(WindowCatalog& catalog,
                                                    ae::ObjId window_id) {
  for (auto const& row : catalog.list->rows) {
    auto* holder = static_cast<apptraverse::WindowHolder*>(&*row);
    if (!holder->window.is_loaded()) {
      continue;
    }
    if (holder->window.id() != window_id) {
      continue;
    }
    MacDesktopWindowPresenter* presenter =
        CatalogWindowPresenter(*holder->window);
    return *presenter;
  }
  apptraverse::WriteFatalStderr(
      "fatal: pending focus window not found in UI catalog\n");
  std::abort();
}

}  // namespace

ATMacApplicationBridge* ATCreateWindowCatalogBridge(
    NSString* userDirectory,
    id<ATMacApplicationBridgeDelegate> delegate) {
  std::filesystem::path const state_dir{
      userDirectory.fileSystemRepresentation};

  struct SessionState {
    ae::ObjId pending_focus_for_ui{};
    ae::ObjId pending_focus_capture{};
    int model_exit_status = 1;
  };
  auto session_state = std::make_shared<SessionState>();

  apptraverse::ui::macos::MacApplicationBridgeCallbacks callbacks;
  callbacks.run_model =
      [state_dir, session_state](apptraverse::ApplicationModelRuntime& runtime,
                                 apptraverse::ApplicationPublishedCallback
                                     on_published) {
        session_state->model_exit_status = apptraverse::RunMacWindowCatalogModel(
            state_dir, runtime, std::move(on_published),
            &session_state->pending_focus_for_ui);
        return session_state->model_exit_status;
      };
  callbacks.close_desktop_window =
      [delegate, session_state](ae::Obj& application_root, ae::Obj& closed_window,
                                apptraverse::ModelObjectProxy& proxy,
                                std::function<void()> request_stop) {
        auto& catalog = static_cast<WindowCatalog&>(application_root);
        if (catalog.main_window.id() == closed_window.obj_id) {
          std::vector<std::uint32_t> const z_order =
              CaptureCatalogDesktopZOrderTopToBottom(catalog, delegate);
          proxy.Invoke(catalog.obj_id,
                       &WindowCatalog::SetDesktopZOrderFromObjIds, z_order);
          request_stop();
          return true;
        }
        proxy.Invoke(catalog.obj_id, &WindowCatalog::UserCloseWindow,
                     closed_window.obj_id);
        return false;
      };
  callbacks.take_incremental_sidecar = [session_state] {
    session_state->pending_focus_capture = session_state->pending_focus_for_ui;
    session_state->pending_focus_for_ui = {};
  };
  callbacks.after_incremental_ui_apply =
      [delegate, session_state](ae::Obj& application_root) {
        ae::ObjId const pending_focus = session_state->pending_focus_capture;
        if (!pending_focus.is_valid()) {
          return;
        }
        auto& catalog = static_cast<WindowCatalog&>(application_root);
        MacDesktopWindowPresenter& presenter =
            FindChildWindowPresenter(catalog, pending_focus);
        [delegate bridgeFocusDesktopPresenter:PresenterToken(&presenter)];
        session_state->pending_focus_capture = {};
      };
  callbacks.after_initial_ui_apply = [delegate](ae::Obj& application_root) {
    RestoreCatalogDesktopZOrder(static_cast<WindowCatalog&>(application_root),
                                delegate);
  };

  return apptraverse::ui::macos::MakeMacApplicationBridge(
      std::move(callbacks), delegate);
}
