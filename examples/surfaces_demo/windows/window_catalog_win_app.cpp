#include "window_catalog_win_app.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/windows/win_application_loading_window.h"
#include "apptraverse/platform/windows/win_application_message_host.h"

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

WindowCatalog& WindowCatalogWinApp::UiCatalog() {
  return static_cast<WindowCatalog&>(ui_session_->root());
}

WindowCatalog const& WindowCatalogWinApp::UiCatalog() const {
  return static_cast<WindowCatalog const&>(ui_session_->root());
}

void WindowCatalogWinApp::OnDesktopWindowUserClose(void* opaque,
                                                   ae::Obj* desktop_window) {
  auto* app = static_cast<WindowCatalogWinApp*>(opaque);
  if (app->IsMainCatalogWindow(*desktop_window)) {
    app->RequestApplicationStop();
    return;
  }
  ae::ObjId const window_id = desktop_window->obj_id;
  app->model_proxy_->Invoke(app->UiCatalog().obj_id,
                            &WindowCatalog::UserCloseWindow, window_id);
}

bool WindowCatalogWinApp::IsMainCatalogWindow(
    ae::Obj const& desktop_window) const {
  return UiCatalog().main_window.id() == desktop_window.obj_id;
}

void WindowCatalogWinApp::RequestApplicationStop() {
  session_.RequestStop();
}

void WindowCatalogWinApp::ApplyPendingFocusWindow(ae::ObjId focus_id) {
  if (!focus_id.is_valid()) {
    return;
  }
  ui::windows::WinDesktopWindowPresenter& presenter =
      FindChildWindowPresenter(UiCatalog(), focus_id);
  HWND const hwnd = presenter.hwnd();
  if (IsIconic(hwnd)) {
    ShowWindow(hwnd, SW_RESTORE);
  }
  SetForegroundWindow(hwnd);
}

void WindowCatalogWinApp::OnInitialPublished() {
  std::vector<std::uint8_t> bytes = session_.runtime.TakePublishedCopy();
  session_.runtime.NotifyPublicationConsumed();
  ui_session_.emplace(bytes, &presentation_host_, *model_proxy_);
}

void WindowCatalogWinApp::OnIncrementalPublished() {
  ae::ObjId focus_window_id{};
  std::vector<std::uint8_t> bytes = session_.runtime.TakePublishedCopy([&] {
    focus_window_id = session_.pending_focus_for_ui;
    session_.pending_focus_for_ui = {};
  });
  session_.runtime.NotifyPublicationConsumed();
  ui_session_->ApplyStructural(bytes);
  ApplyPendingFocusWindow(focus_window_id);
}

int WindowCatalogWinApp::Run(std::filesystem::path const& state_dir) {
  session_.state_dir = state_dir;
  model_proxy_.emplace([this](ModelObjectProxy::ModelWork work) {
    session_.Post(std::move(work));
  });

  presentation_host_.opaque = this;
  presentation_host_.on_desktop_window_user_close = &OnDesktopWindowUserClose;

  windows::WinApplicationLoadingWindow loading(
      L"AppTraverseWindowCatalogLoading", L"Loading catalog", L"Loading catalog",
      200, 200, 320, 120);

  windows::WinApplicationMessageHost message_host(
      [this, &loading] {
        OnInitialPublished();
        loading.Close();
      },
      [this] { OnIncrementalPublished(); },
      [this] { RequestApplicationStop(); });

  presentation_host_.notify_hwnd = message_host.notify_hwnd();

  message_host.StartModelThread([this, &message_host] {
    session_.Run([&message_host](WindowCatalogPublicationKind kind) {
      message_host.NotifyPublished(
          kind == WindowCatalogPublicationKind::Initial
              ? ApplicationPublicationKind::Initial
              : ApplicationPublicationKind::Incremental);
    });
  });

  message_host.PumpUntilModelDone([this, &loading] {
    if (!loading.is_open()) {
      RequestApplicationStop();
    }
  });

  message_host.JoinModelThread();

  ui_session_.reset();
  model_proxy_.reset();

  return 0;
}

}  // namespace apptraverse
