#include "apptraverse/platform/windows/win_application_message_host.h"

#include "apptraverse/platform/windows/win32_fatal.h"

namespace apptraverse {
namespace windows {
namespace {

wchar_t const kNotifyWindowClass[] = L"AppTraverseApplicationNotify";

constexpr UINT kInitialPublishedMessage = WM_APP + 1;
constexpr UINT kIncrementalPublishedMessage = WM_APP + 2;

UINT MessageForPublicationKind(ApplicationPublicationKind kind) {
  if (kind == ApplicationPublicationKind::Initial) {
    return kInitialPublishedMessage;
  }
  return kIncrementalPublishedMessage;
}

}  // namespace

WinApplicationMessageHost::WinApplicationMessageHost(Callback on_initial,
                                                     Callback on_incremental,
                                                     Callback on_stop)
    : on_initial_{std::move(on_initial)},
      on_incremental_{std::move(on_incremental)},
      on_stop_{std::move(on_stop)} {
  instance_ = GetModuleHandleW(nullptr);

  WNDCLASSW wc{};
  wc.lpfnWndProc = &WinApplicationMessageHost::NotifyWndProc;
  wc.hInstance = instance_;
  wc.lpszClassName = kNotifyWindowClass;
  if (RegisterClassW(&wc) == 0) {
    FatalWin32("RegisterClassW AppTraverseApplicationNotify", GetLastError());
  }

  done_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (done_event_ == nullptr) {
    FatalWin32("CreateEventW model done", GetLastError());
  }

  notify_hwnd_ =
      CreateWindowExW(0, kNotifyWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                        nullptr, instance_, this);
  if (notify_hwnd_ == nullptr) {
    FatalWin32("CreateWindowExW AppTraverseApplicationNotify", GetLastError());
  }
}

WinApplicationMessageHost::~WinApplicationMessageHost() {
  JoinModelThread();

  if (notify_hwnd_ != nullptr) {
    if (DestroyWindow(notify_hwnd_) == 0) {
      FatalWin32("DestroyWindow AppTraverseApplicationNotify", GetLastError());
    }
    notify_hwnd_ = nullptr;
  }

  if (instance_ != nullptr) {
    if (UnregisterClassW(kNotifyWindowClass, instance_) == 0) {
      FatalWin32("UnregisterClassW AppTraverseApplicationNotify", GetLastError());
    }
  }

  if (done_event_ != nullptr) {
    if (CloseHandle(done_event_) == 0) {
      FatalWin32("CloseHandle model done", GetLastError());
    }
    done_event_ = nullptr;
  }
}

void WinApplicationMessageHost::StartModelThread(ModelThreadBody run_model) {
  model_thread_ = std::thread([this, body = std::move(run_model)]() mutable {
    body();
    if (SetEvent(done_event_) == 0) {
      FatalWin32("SetEvent model done", GetLastError());
    }
  });
}

void WinApplicationMessageHost::NotifyPublished(ApplicationPublicationKind kind) {
  UINT const message = MessageForPublicationKind(kind);
  if (PostMessageW(notify_hwnd_, message, 0, 0) == 0) {
    FatalWin32("PostMessageW publication notify", GetLastError());
  }
}

void WinApplicationMessageHost::PumpUntilModelDone(Callback on_quit) {
  for (;;) {
    HANDLE handles[] = {done_event_};
    DWORD const wait =
        MsgWaitForMultipleObjects(1, handles, FALSE, INFINITE,
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
        if (on_quit) {
          on_quit();
        }
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
}

void WinApplicationMessageHost::JoinModelThread() {
  if (model_thread_.joinable()) {
    model_thread_.join();
  }
}

LRESULT CALLBACK WinApplicationMessageHost::NotifyWndProc(HWND hwnd, UINT msg,
                                                          WPARAM wparam,
                                                          LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* host = reinterpret_cast<WinApplicationMessageHost*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (host == nullptr) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  return host->HandleNotifyMessage(msg, wparam, lparam);
}

LRESULT WinApplicationMessageHost::HandleNotifyMessage(UINT msg, WPARAM wparam,
                                                        LPARAM lparam) {
  if (msg == kInitialPublishedMessage) {
    if (on_initial_) {
      on_initial_();
    }
    return 0;
  }
  if (msg == kIncrementalPublishedMessage) {
    if (on_incremental_) {
      on_incremental_();
    }
    return 0;
  }
  if (msg == WM_CLOSE) {
    if (on_stop_) {
      on_stop_();
    }
    return 0;
  }
  return DefWindowProcW(notify_hwnd_, msg, wparam, lparam);
}

}  // namespace windows
}  // namespace apptraverse
