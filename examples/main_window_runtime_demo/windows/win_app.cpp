#include "win_app.h"

#include <cassert>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"

#include "main_window_ids.h"

namespace apptraverse {
namespace {

void RegisterWindowClasses() {
  WNDCLASSW notify{};
  notify.lpfnWndProc = &WinApp::WndProc;
  notify.hInstance = GetModuleHandleW(nullptr);
  notify.lpszClassName = kNotifyWindowClass;
  RegisterClassW(&notify);

  WNDCLASSW loading{};
  loading.lpfnWndProc = &WinApp::WndProc;
  loading.hInstance = GetModuleHandleW(nullptr);
  loading.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  loading.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  loading.lpszClassName = kLoadingWindowClass;
  RegisterClassW(&loading);

  WNDCLASSW main{};
  main.lpfnWndProc = &WinApp::WndProc;
  main.hInstance = GetModuleHandleW(nullptr);
  main.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  main.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  main.lpszClassName = kMainWindowClass;
  RegisterClassW(&main);
}

void PaintLoading(HWND hwnd) {
  PAINTSTRUCT ps{};
  HDC hdc = BeginPaint(hwnd, &ps);
  RECT client{};
  GetClientRect(hwnd, &client);
  DrawTextW(hdc, L"Loading", -1, &client,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  EndPaint(hwnd, &ps);
}

}  // namespace

LRESULT CALLBACK WinApp::WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* app = reinterpret_cast<WinApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (app != nullptr) {
    return app->Handle(hwnd, msg, wparam, lparam);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT WinApp::Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_APPTRAVERSE_PUBLISHED) {
    OnPublished();
    return 0;
  }
  if (msg == WM_PAINT && hwnd == loading_) {
    PaintLoading(hwnd);
    return 0;
  }
  if (msg == WM_CLOSE) {
    RequestStop();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void WinApp::OnPublished() {
  if (stop_requested_ || ui_application_) {
    return;
  }
  auto bytes = session_.channel.TakePublishedCopy();
  if (bytes.empty()) {
    return;
  }
  ui_domain_ = std::make_unique<ae::Domain>(ui_storage_);
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  auto ui_root = LoadInitialPublication(in, *ui_domain_, ui_storage_);
  ui_application_ = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  assert(ui_application_);
  assert(ui_application_->main_window);
  presenter_.Create(*ui_application_->main_window, this);
  assert(presenter_.hwnd != nullptr);
  ShowWindow(presenter_.hwnd, SW_SHOW);
  UpdateWindow(presenter_.hwnd);
  if (loading_ != nullptr) {
    DestroyWindow(loading_);
    loading_ = nullptr;
  }
}

void WinApp::RequestStop() {
  if (stop_requested_) {
    return;
  }
  stop_requested_ = true;
  accept_input_ = false;
  session_.RequestStop();
}

void WinApp::DestroyGuiMirror() {
  presenter_.Destroy();
  ui_application_ = {};
  ui_domain_.reset();
}

void WinApp::SetHoldStage(ModelStartupStage stage) {
  session_.hold_stage.store(static_cast<int>(stage), std::memory_order_release);
}

int WinApp::Run(std::filesystem::path const& state_dir) {
  EnableNoninteractiveCrt();
  EnsureMainWindowRegistration();
  RegisterWindowClasses();

  session_.state_dir = state_dir;
  session_.done_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  assert(session_.done_event != nullptr);

  notify_ = CreateWindowExW(0, kNotifyWindowClass, L"", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr),
                            this);
  assert(notify_ != nullptr);
  session_.notify_hwnd.store(reinterpret_cast<std::uintptr_t>(notify_),
                              std::memory_order_release);

  loading_ = CreateWindowExW(
      0, kLoadingWindowClass, kLoadingWindowTitle, WS_OVERLAPPED | WS_CAPTION |
                                                     WS_SYSMENU | WS_VISIBLE,
      200, 200, 280, 120, nullptr, nullptr, GetModuleHandleW(nullptr), this);
  assert(loading_ != nullptr);

  model_thread_ = std::thread([this] { session_.Run(); });

  for (;;) {
    HANDLE handles[] = {session_.done_event};
    DWORD const wait = MsgWaitForMultipleObjects(1, handles, FALSE, INFINITE,
                                                QS_ALLINPUT | QS_ALLPOSTMESSAGE);
    if (wait == WAIT_OBJECT_0) {
      break;
    }
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
      if (msg.message == WM_QUIT) {
        RequestStop();
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  if (model_thread_.joinable()) {
    model_thread_.join();
  }
  if (loading_ != nullptr) {
    DestroyWindow(loading_);
    loading_ = nullptr;
  }
  if (notify_ != nullptr) {
    DestroyWindow(notify_);
    notify_ = nullptr;
  }
  DestroyGuiMirror();
  if (session_.done_event != nullptr) {
    CloseHandle(session_.done_event);
    session_.done_event = nullptr;
  }
  return 0;
}

}  // namespace apptraverse
