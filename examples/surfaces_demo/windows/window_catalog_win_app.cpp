#include "window_catalog_win_app.h"

#include "apptraverse/platform/windows/win32_fatal.h"

#include "apptraverse/object_serialization.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_window_presenter.h"
#include "apptraverse/surfaces/surfaces_win32_messages.h"

#include "window_catalog_ids.h"

namespace apptraverse {
namespace {

wchar_t const kNotifyWindowClass[] = L"AppTraverseWindowCatalogNotify";
wchar_t const kLoadingWindowClass[] = L"AppTraverseWindowCatalogLoading";
wchar_t const kLoadingWindowTitle[] = L"Loading catalog";

void PaintLoading(HWND hwnd) {
  PAINTSTRUCT ps{};
  HDC hdc = BeginPaint(hwnd, &ps);
  RECT client{};
  GetClientRect(hwnd, &client);
  DrawTextW(hdc, L"Loading catalog", -1, &client,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  EndPaint(hwnd, &ps);
}

}  // namespace

void WindowCatalogWinApp::OnDesktopWindowUserClose(void* opaque,
                                                   ae::Obj* desktop_window) {
  if (opaque == nullptr || desktop_window == nullptr) {
    return;
  }
  auto* app = static_cast<WindowCatalogWinApp*>(opaque);
  if (app->IsMainCatalogWindow(*desktop_window)) {
    app->RequestApplicationStop();
    return;
  }
  ae::ObjId const window_id = desktop_window->obj_id;
  if (!app->model_proxy_ || !app->ui_catalog_) {
    return;
  }
  app->model_proxy_->Invoke(app->ui_catalog_->obj_id,
                            &WindowCatalog::UserCloseWindow, window_id);
}

bool WindowCatalogWinApp::IsMainCatalogWindow(
    ae::Obj const& desktop_window) const {
  if (!ui_catalog_ || !ui_catalog_->main_window.is_valid()) {
    return false;
  }
  return ui_catalog_->main_window.id() == desktop_window.obj_id;
}

LRESULT CALLBACK WindowCatalogWinApp::WndProc(HWND hwnd, UINT msg,
                                              WPARAM wparam, LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* app =
      reinterpret_cast<WindowCatalogWinApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (app == nullptr) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  return app->Handle(hwnd, msg, wparam, lparam);
}

void WindowCatalogWinApp::RequestApplicationStop() {
  session_.RequestStop();
}

LRESULT WindowCatalogWinApp::Handle(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam) {
  (void)wparam;
  (void)lparam;
  if (msg == WM_APPTRAVERSE_INITIAL_PUBLISHED) {
    OnInitialPublished();
    return 0;
  }
  if (msg == WM_APPTRAVERSE_INCREMENTAL_PUBLISHED) {
    OnIncrementalPublished();
    return 0;
  }
  if (msg == WM_PAINT && hwnd == loading_) {
    PaintLoading(hwnd);
    return 0;
  }
  if (msg == WM_CLOSE) {
    if (hwnd == loading_) {
      return 0;
    }
    RequestApplicationStop();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

namespace {

ui::windows::WinDesktopWindowPresenter* FindChildWindowPresenter(
    WindowCatalog& catalog, ae::ObjId window_id) {
  if (!catalog.list) {
    return nullptr;
  }
  for (auto const& row : catalog.list->rows) {
    if (!row) {
      continue;
    }
    auto* holder = static_cast<WindowHolder*>(&*row);
    if (!holder->window.is_valid() || !holder->window.is_loaded()) {
      continue;
    }
    if (holder->window.id() != window_id) {
      continue;
    }
    auto& presented =
        static_cast<ui::UiPresentedWindow&>(*holder->window.operator->());
    if (!presented.presenter.is_valid() || !presented.presenter.is_loaded()) {
      return nullptr;
    }
    ui::UiWindowPresenter* base = presented.presenter.operator->();
    auto& registry = ae::Registry::GetRegistry();
    if (registry.GenerationDistance(
            ui::windows::WinDesktopWindowPresenter::kClassId,
            base->GetClassId()) < 0) {
      return nullptr;
    }
    return static_cast<ui::windows::WinDesktopWindowPresenter*>(base);
  }
  return nullptr;
}

}  // namespace

void WindowCatalogWinApp::ApplyPendingFocusWindow(
    std::uint32_t focus_window_id) {
  if (!ui_catalog_ || !model_proxy_) {
    return;
  }
  ae::ObjId const focus_id{focus_window_id};
  if (!focus_id.is_valid()) {
    return;
  }
  ui::windows::WinDesktopWindowPresenter* presenter =
      FindChildWindowPresenter(*ui_catalog_, focus_id);
  if (presenter == nullptr || presenter->hwnd() == nullptr) {
    return;
  }
  HWND const hwnd = presenter->hwnd();
  if (IsIconic(hwnd)) {
    ShowWindow(hwnd, SW_RESTORE);
  }
  SetForegroundWindow(hwnd);
}

void WindowCatalogWinApp::OnInitialPublished() {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();
  ui_domain_ = std::make_unique<ae::Domain>(ui_storage_);
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  auto ui_root = LoadInitialPublication(in, *ui_domain_, ui_storage_);
  ui_catalog_ =
      WindowCatalog::ptr::MakeFromThis(static_cast<WindowCatalog*>(ui_root.get()));
  InitializePresenters(*ui_catalog_, &presentation_host_, &*model_proxy_);
  ApplyPendingFocusWindow(0);
  if (DestroyWindow(loading_) == 0) {
    FatalWin32("DestroyWindow Loading", GetLastError());
  }
  loading_ = nullptr;
}

void WindowCatalogWinApp::OnIncrementalPublished() {
  std::vector<std::uint8_t> bytes;
  std::uint32_t focus_window_id = 0;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
    focus_window_id = session_.pending_focus_for_ui;
    session_.pending_focus_for_ui = 0;
  }
  session_.cv.notify_all();
  ApplyWindowCatalogStructural(bytes, *ui_catalog_, ui_storage_,
                               &presentation_host_, &*model_proxy_);
  ApplyPendingFocusWindow(focus_window_id);
}

int WindowCatalogWinApp::Run(std::filesystem::path const& state_dir) {
  HINSTANCE const instance = GetModuleHandleW(nullptr);

  WNDCLASSW notify_wc{};
  notify_wc.lpfnWndProc = &WindowCatalogWinApp::WndProc;
  notify_wc.hInstance = instance;
  notify_wc.lpszClassName = kNotifyWindowClass;
  if (RegisterClassW(&notify_wc) == 0) {
    FatalWin32("RegisterClassW notify", GetLastError());
  }

  WNDCLASSW loading_wc{};
  loading_wc.lpfnWndProc = &WindowCatalogWinApp::WndProc;
  loading_wc.hInstance = instance;
  loading_wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  loading_wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  loading_wc.lpszClassName = kLoadingWindowClass;
  if (RegisterClassW(&loading_wc) == 0) {
    FatalWin32("RegisterClassW Loading", GetLastError());
  }

  session_.state_dir = state_dir;
  model_proxy_.emplace([this](ModelObjectProxy::ModelWork work) {
    session_.Post(std::move(work));
  });

  presentation_host_.notify_hwnd = nullptr;
  presentation_host_.opaque = this;
  presentation_host_.on_desktop_window_user_close = &OnDesktopWindowUserClose;

  HANDLE done_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (done_event == nullptr) {
    FatalWin32("CreateEventW done_event", GetLastError());
  }

  notify_ = CreateWindowExW(0, kNotifyWindowClass, L"", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, instance, this);
  if (notify_ == nullptr) {
    FatalWin32("CreateWindowExW notify", GetLastError());
  }
  presentation_host_.notify_hwnd = notify_;

  loading_ = CreateWindowExW(
      0, kLoadingWindowClass, kLoadingWindowTitle,
      WS_OVERLAPPED | WS_CAPTION | WS_VISIBLE, 200, 200, 320, 120, nullptr,
      nullptr, instance, this);
  if (loading_ == nullptr) {
    FatalWin32("CreateWindowExW Loading", GetLastError());
  }

  HWND const notify = notify_;
  model_thread_ = std::thread([this, notify, done_event] {
    session_.Run([notify](WindowCatalogPublicationKind kind) {
      UINT const message = kind == WindowCatalogPublicationKind::Initial
                               ? WM_APPTRAVERSE_INITIAL_PUBLISHED
                               : WM_APPTRAVERSE_INCREMENTAL_PUBLISHED;
      if (PostMessageW(notify, message, 0, 0) == 0) {
        FatalWin32("PostMessageW publication", GetLastError());
      }
    });
    if (SetEvent(done_event) == 0) {
      FatalWin32("SetEvent done_event", GetLastError());
    }
  });

  for (;;) {
    HANDLE handles[] = {done_event};
    DWORD const wait = MsgWaitForMultipleObjects(1, handles, FALSE, INFINITE,
                                                 QS_ALLINPUT | QS_ALLPOSTMESSAGE);
    if (wait == WAIT_FAILED) {
      FatalWin32("MsgWaitForMultipleObjects", GetLastError());
    }
    if (wait == WAIT_OBJECT_0) {
      break;
    }
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
      if (msg.message == WM_QUIT) {
        if (loading_ == nullptr) {
          RequestApplicationStop();
        }
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  model_thread_.join();
  if (ui_catalog_) {
    UnloadPresenters(*ui_catalog_);
  }
  ui_catalog_ = {};
  ui_domain_.reset();
  model_proxy_.reset();
  if (notify_ != nullptr) {
    if (DestroyWindow(notify_) == 0) {
      FatalWin32("DestroyWindow notify", GetLastError());
    }
  }
  if (UnregisterClassW(kNotifyWindowClass, instance) == 0) {
    FatalWin32("UnregisterClassW notify", GetLastError());
  }
  if (UnregisterClassW(kLoadingWindowClass, instance) == 0) {
    FatalWin32("UnregisterClassW Loading", GetLastError());
  }
  if (CloseHandle(done_event) == 0) {
    FatalWin32("CloseHandle done_event", GetLastError());
  }
  return session_.shutdown_exit_code;
}

}  // namespace apptraverse
