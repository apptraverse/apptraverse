#include "win_app.h"

#include <cassert>

#include "apptraverse/object_serialization.h"

#include "win_presenters.h"

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
  if (app == nullptr) {
    // WM_GETMINMAXINFO is sent before WM_NCCREATE for overlapped windows.
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  return app->Handle(hwnd, msg, wparam, lparam);
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
    if (hwnd == loading_) {
      return 0;
    }
    session_.RequestStop();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void WinApp::OnPublished() {
  auto bytes = session_.channel.TakePublishedCopy();
  ui_domain_ = std::make_unique<ae::Domain>(ui_storage_);
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  auto ui_root = LoadInitialPublication(in, *ui_domain_, ui_storage_);
  ui_application_ = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  InitializePresenters(*ui_application_, this);
  DestroyWindow(loading_);
  loading_ = nullptr;
}

int WinApp::Run(std::filesystem::path const& state_dir) {
  RegisterWindowClasses();

  session_.state_dir = state_dir;
  session_.done_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  assert(session_.done_event != nullptr && "CreateEventW done_event failed");

  notify_ = CreateWindowExW(0, kNotifyWindowClass, L"", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr),
                            this);
  assert(notify_ != nullptr && "CreateWindowExW notify failed");
  session_.notify_hwnd.store(reinterpret_cast<std::uintptr_t>(notify_),
                              std::memory_order_release);

  // Loading is not a user window: no system Close. WM_CLOSE is ignored.
  loading_ = CreateWindowExW(
      0, kLoadingWindowClass, kLoadingWindowTitle,
      WS_OVERLAPPED | WS_CAPTION | WS_VISIBLE, 200, 200, 280, 120, nullptr,
      nullptr, GetModuleHandleW(nullptr), this);
  assert(loading_ != nullptr && "CreateWindowExW Loading failed");

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
      // Startup is not cancelable. Ignore WM_QUIT while Loading is still up.
      // After OnPublished, loading_ is null and Main exists; then stop is allowed.
      if (msg.message == WM_QUIT) {
        if (loading_ == nullptr) {
          session_.RequestStop();
        }
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  model_thread_.join();
  // Tear down GUI presentation before destroying the UI Domain.
  UnloadPresenters(*ui_application_);
  ui_application_ = {};
  ui_domain_.reset();
  DestroyWindow(notify_);
  CloseHandle(session_.done_event);
  return 0;
}

}  // namespace apptraverse
