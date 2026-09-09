#include "win_app.h"

#include "apptraverse/object_serialization.h"
#include "dynamic_win32_messages.h"
#include "win32_fatal.h"
#include "win_presenters.h"

namespace apptraverse {
namespace {

wchar_t const kNotifyWindowClass[] = L"AppTraverseDynamicNotify";
wchar_t const kLoadingWindowClass[] = L"AppTraverseDynamicLoading";
wchar_t const kLoadingWindowTitle[] = L"Loading";

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
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  return app->Handle(hwnd, msg, wparam, lparam);
}

LRESULT WinApp::Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  (void)lparam;
  if (msg == WM_APPTRAVERSE_INITIAL_PUBLISHED) {
    OnInitialPublished();
    return 0;
  }
  if (msg == WM_APPTRAVERSE_INCREMENTAL_PUBLISHED) {
    OnIncrementalPublished();
    return 0;
  }
  if (msg == WM_APPTRAVERSE_ADD_ITEM) {
    ++add_sequence_;
    session_.SubmitAddItem(AddItemCommand{add_sequence_});
    return 0;
  }
  if (msg == WM_APPTRAVERSE_REMOVE_ITEM) {
    session_.SubmitRemoveItem(
        RemoveItemCommand{ae::ObjId{static_cast<ae::ObjId::Type>(wparam)}});
    return 0;
  }
  if (msg == WM_APPTRAVERSE_CLOSE_WINDOW) {
    // Single MainWindow demo: closing that window stops the application.
    if (ui_application_.is_valid() &&
        ui_application_->main_window.is_valid() &&
        ui_application_->main_window->obj_id.id() ==
            static_cast<ae::ObjId::Type>(wparam)) {
      session_.RequestStop();
    }
    return 0;
  }
  if (msg == WM_APPTRAVERSE_STOP) {
    session_.RequestStop();
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

void WinApp::OnInitialPublished() {
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
  ui_application_ = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  InitializePresenters(*ui_application_, notify_);
  if (DestroyWindow(loading_) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Loading", err);
  }
  loading_ = nullptr;
}

void WinApp::OnIncrementalPublished() {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();
  ApplyItemListStructural(bytes, *ui_application_, ui_storage_, notify_);
}

int WinApp::Run(std::filesystem::path const& state_dir) {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  RegisterDynamicWin32Classes();

  WNDCLASSW notify_wc{};
  notify_wc.lpfnWndProc = &WinApp::WndProc;
  notify_wc.hInstance = instance;
  notify_wc.lpszClassName = kNotifyWindowClass;
  if (RegisterClassW(&notify_wc) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("RegisterClassW notify", err);
  }

  WNDCLASSW loading_wc{};
  loading_wc.lpfnWndProc = &WinApp::WndProc;
  loading_wc.hInstance = instance;
  loading_wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  loading_wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  loading_wc.lpszClassName = kLoadingWindowClass;
  if (RegisterClassW(&loading_wc) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("RegisterClassW Loading", err);
  }

  session_.state_dir = state_dir;
  HANDLE done_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (done_event == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateEventW done_event", err);
  }

  notify_ = CreateWindowExW(0, kNotifyWindowClass, L"", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, instance, this);
  if (notify_ == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW notify", err);
  }

  loading_ = CreateWindowExW(
      0, kLoadingWindowClass, kLoadingWindowTitle,
      WS_OVERLAPPED | WS_CAPTION | WS_VISIBLE, 200, 200, 280, 120, nullptr,
      nullptr, instance, this);
  if (loading_ == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Loading", err);
  }

  HWND const notify = notify_;
  model_thread_ = std::thread([this, notify, done_event] {
    session_.Run([notify](PublicationKind kind) {
      UINT const message = kind == PublicationKind::Initial
                               ? WM_APPTRAVERSE_INITIAL_PUBLISHED
                               : WM_APPTRAVERSE_INCREMENTAL_PUBLISHED;
      if (PostMessageW(notify, message, 0, 0) == 0) {
        DWORD const err = GetLastError();
        FatalWin32("PostMessageW publication", err);
      }
    });
    if (SetEvent(done_event) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("SetEvent done_event", err);
    }
  });

  for (;;) {
    HANDLE handles[] = {done_event};
    DWORD const wait = MsgWaitForMultipleObjects(1, handles, FALSE, INFINITE,
                                                 QS_ALLINPUT | QS_ALLPOSTMESSAGE);
    if (wait == WAIT_FAILED) {
      DWORD const err = GetLastError();
      FatalWin32("MsgWaitForMultipleObjects", err);
    }
    if (wait == WAIT_OBJECT_0) {
      break;
    }
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
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
  UnloadPresenters(*ui_application_);
  ui_application_ = {};
  ui_domain_.reset();
  UnregisterDynamicWin32Classes();
  if (DestroyWindow(notify_) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow notify", err);
  }
  if (UnregisterClassW(kNotifyWindowClass, instance) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("UnregisterClassW notify", err);
  }
  if (UnregisterClassW(kLoadingWindowClass, instance) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("UnregisterClassW Loading", err);
  }
  if (CloseHandle(done_event) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("CloseHandle done_event", err);
  }
  return 0;
}

}  // namespace apptraverse
