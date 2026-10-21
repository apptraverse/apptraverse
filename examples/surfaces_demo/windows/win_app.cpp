#include "win_app.h"

#include "surfaces_shutdown_trace.h"

#include "apptraverse/object_serialization.h"
#include "apptraverse/surfaces/surfaces_win32_messages.h"
#include "win32_fatal.h"
#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/platform/windows/surfaces_win32_z_order.h"
#include "apptraverse/platform/windows/surfaces_win_registration.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/surfaces/surfaces_demo_revision.h"
#include "apptraverse/surfaces/surfaces_ids.h"

namespace apptraverse {
namespace {

using surfaces_demo::ObjId;
using surfaces_demo::ToObjId;

constexpr ae::ObjId kSurface1Id{ToObjId(ObjId::Surface1)};

wchar_t const kNotifyWindowClass[] = L"AppTraverseSurfacesNotify";
wchar_t const kLoadingWindowClass[] = L"AppTraverseSurfacesLoading";
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

void WinApp::QueueAllWindowBounds() {
  for (auto const& surface : ui_application_->surfaces->surfaces) {
    if (Win32SurfaceWindowPresenter::ptr window =
            ResolveWin32WindowPresenter(*surface)) {
      window->QueueCurrentBounds();
    }
  }
}

void WinApp::QueueDesktopZOrderSnapshot() {
  if (!ui_application_ || !model_proxy_) {
    return;
  }
  DWORD const pid = GetCurrentProcessId();
  std::vector<HWND> const host_windows =
      EnumerateSurfaceHostWindowsTopToBottom(pid);
  std::vector<std::uint32_t> ordered_obj_ids;
  ordered_obj_ids.reserve(host_windows.size());
  for (HWND hwnd : host_windows) {
    for (auto const& surface : ui_application_->surfaces->surfaces) {
      if (Win32SurfaceWindowPresenter::ptr window =
              ResolveWin32WindowPresenter(*surface)) {
        if (window->hwnd == hwnd) {
          ordered_obj_ids.push_back(surface->obj_id.id());
          break;
        }
      }
    }
  }
  model_proxy_->Invoke<Surfaces>(
      ui_application_->surfaces->obj_id, &Surfaces::SetDesktopZOrderFromObjIds,
      std::move(ordered_obj_ids));
}

void WinApp::QueueForegroundAsCurrent() {
  HWND const foreground = GetForegroundWindow();
  for (auto const& surface : ui_application_->surfaces->surfaces) {
    if (Win32SurfaceWindowPresenter::ptr window =
            ResolveWin32WindowPresenter(*surface)) {
      if (window->hwnd == foreground) {
        window->PageShown();
        return;
      }
    }
  }
}

void WinApp::RestoreDesktopSurfaceZOrder() {
  Surfaces& surfaces_node = *ui_application_->surfaces;
  std::vector<HWND> hwnds_top_to_bottom;
  if (!surfaces_node.desktop_z_order.empty()) {
    hwnds_top_to_bottom.reserve(surfaces_node.desktop_z_order.size());
    for (auto const& surface : surfaces_node.desktop_z_order) {
      if (Win32SurfaceWindowPresenter::ptr window =
              ResolveWin32WindowPresenter(*surface)) {
        if (window->hwnd != nullptr) {
          hwnds_top_to_bottom.push_back(window->hwnd);
        }
      }
    }
  } else {
    Surface::ptr target = surfaces_node.mobile_current;
    if (!target) {
      if (surfaces_node.surfaces.empty()) {
        return;
      }
      target = surfaces_node.surfaces.back();
    }
    if (Win32SurfaceWindowPresenter::ptr window =
            ResolveWin32WindowPresenter(*target)) {
      if (window->hwnd != nullptr) {
        hwnds_top_to_bottom.push_back(window->hwnd);
      }
    }
  }
  if (hwnds_top_to_bottom.empty()) {
    return;
  }
  SetSurfacesWin32ZOrderRestoreInProgress(true);
  ApplySurfaceHostWindowsZOrderTopToBottom(hwnds_top_to_bottom);
  SetSurfacesWin32ZOrderRestoreInProgress(false);

  Surface::ptr const focus = surfaces_node.mobile_current;
  if (!focus) {
    return;
  }
  if (Win32SurfaceWindowPresenter::ptr window =
          ResolveWin32WindowPresenter(*focus)) {
    if (window->hwnd != nullptr) {
      SetForegroundWindow(window->hwnd);
    }
  }
}

void WinApp::SetSessionAutomation(
    std::vector<SurfacesSessionAutomationStep> steps) {
  session_automation_ = std::move(steps);
  session_automation_index_ = 0;
  session_automation_waiting_publication_ = false;
  session_automation_failed_ = false;
}

Surface* WinApp::FindUiSurface1() {
  if (!ui_application_) {
    return nullptr;
  }
  for (auto const& surface : ui_application_->surfaces->surfaces) {
    if (surface->obj_id == kSurface1Id) {
      return &*surface;
    }
  }
  return nullptr;
}

void WinApp::ContinueSessionAutomationAfterPublication() {
  if (!session_automation_waiting_publication_) {
    return;
  }
  session_automation_waiting_publication_ = false;
  if (notify_ != nullptr) {
    if (PostMessageW(notify_, WM_APPTRAVERSE_SESSION_AUTOMATION, 0, 0) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("PostMessageW session automation continue", err);
    }
  }
}

void WinApp::RunNextSessionAutomationStep() {
  if (session_automation_failed_ ||
      session_automation_index_ >= session_automation_.size()) {
    return;
  }
  SurfacesSessionAutomationStep const& step =
      session_automation_[session_automation_index_++];
  if (step.op == SurfacesSessionAutomationOp::Shutdown) {
    RequestApplicationStop();
    return;
  }
  Surface* surface = FindUiSurface1();
  if (surface == nullptr) {
    session_automation_failed_ = true;
    session_.shutdown_exit_code = 1;
    RequestApplicationStop();
    return;
  }
  Win32SurfaceWindowPresenter::ptr window =
      ResolveWin32WindowPresenter(*surface);
  if (!window || window->hwnd == nullptr) {
    session_automation_failed_ = true;
    session_.shutdown_exit_code = 1;
    RequestApplicationStop();
    return;
  }
  if (step.op == SurfacesSessionAutomationOp::VerifyBounds) {
    if (surface->desktop_x != step.x || surface->desktop_y != step.y ||
        surface->desktop_width != step.width ||
        surface->desktop_height != step.height) {
      session_automation_failed_ = true;
      session_.shutdown_exit_code = 1;
      RequestApplicationStop();
      return;
    }
    if (notify_ != nullptr) {
      if (PostMessageW(notify_, WM_APPTRAVERSE_SESSION_AUTOMATION, 0, 0) == 0) {
        DWORD const err = GetLastError();
        FatalWin32("PostMessageW session automation verify bounds", err);
      }
    }
    return;
  }
  if (step.op == SurfacesSessionAutomationOp::SetBounds) {
    if (SetWindowPos(window->hwnd, nullptr, step.x, step.y, step.width,
                     step.height,
                     SWP_NOZORDER | SWP_NOACTIVATE) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("SetWindowPos Surface automation", err);
    }
    window->QueueCurrentBounds();
    session_automation_waiting_publication_ = true;
    return;
  }
#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
  if (step.op == SurfacesSessionAutomationOp::SetSuffix) {
    model_proxy_->Invoke<Surface>(surface->obj_id,
                                  &Surface::SetWindowTitleSuffix, step.text);
    session_automation_waiting_publication_ = true;
    return;
  }
#endif
  if (step.op == SurfacesSessionAutomationOp::VerifyTitleSuffix) {
    wchar_t buffer[512]{};
    GetWindowTextW(window->hwnd, buffer, 512);
    std::string const title = ui::windows::WideToUtf8(buffer);
    if (title.find(step.text) == std::string::npos) {
      session_automation_failed_ = true;
      session_.shutdown_exit_code = 1;
      RequestApplicationStop();
      return;
    }
    if (notify_ != nullptr) {
      if (PostMessageW(notify_, WM_APPTRAVERSE_SESSION_AUTOMATION, 0, 0) == 0) {
        DWORD const err = GetLastError();
        FatalWin32("PostMessageW session automation verify", err);
      }
    }
    return;
  }
  session_automation_failed_ = true;
  session_.shutdown_exit_code = 1;
  RequestApplicationStop();
}

void WinApp::RequestApplicationStop() {
  SurfacesShutdownTrace::MarkCloseRequested();
  if (ui_application_) {
    QueueAllWindowBounds();
    QueueDesktopZOrderSnapshot();
    QueueForegroundAsCurrent();
  }
  SurfacesShutdownTrace::MarkCloseUiStateQueued();
  session_.RequestStop();
}

LRESULT WinApp::Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  (void)lparam;
  (void)wparam;
  if (msg == WM_APPTRAVERSE_INITIAL_PUBLISHED) {
    OnInitialPublished();
    return 0;
  }
  if (msg == WM_APPTRAVERSE_INCREMENTAL_PUBLISHED) {
    OnIncrementalPublished();
    return 0;
  }
  if (msg == WM_APPTRAVERSE_STOP) {
    RequestApplicationStop();
    return 0;
  }
  if (msg == WM_APPTRAVERSE_SESSION_AUTOMATION) {
    RunNextSessionAutomationStep();
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
  InitializePresenters(*ui_application_, notify_, &*model_proxy_);
  RestoreDesktopSurfaceZOrder();
  if (DestroyWindow(loading_) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Loading", err);
  }
  loading_ = nullptr;
  if (!session_automation_.empty() && notify_ != nullptr) {
    if (PostMessageW(notify_, WM_APPTRAVERSE_SESSION_AUTOMATION, 0, 0) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("PostMessageW session automation start", err);
    }
  }
}

void WinApp::OnIncrementalPublished() {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();
  ApplySurfacesStructural(bytes, *ui_application_, ui_storage_, notify_,
                          &*model_proxy_);
  ContinueSessionAutomationAfterPublication();
}

int WinApp::Run(std::filesystem::path const& state_dir) {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  EnsureWin32SurfacePresenterRegistration();
  RegisterSurfacesWin32Classes();

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
  model_proxy_.emplace([this](ModelObjectProxy::ModelWork work) {
    session_.Post(std::move(work));
  });
  session_.presentation_size_queue.BindProxy(&*model_proxy_);

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
    session_.Run([notify](SurfacesPublicationKind kind) {
      UINT const message = kind == SurfacesPublicationKind::Initial
                               ? WM_APPTRAVERSE_INITIAL_PUBLISHED
                               : WM_APPTRAVERSE_INCREMENTAL_PUBLISHED;
      if (PostMessageW(notify, message, 0, 0) == 0) {
        DWORD const err = GetLastError();
        FatalWin32("PostMessageW publication", err);
      }
    });
    SurfacesShutdownTrace::MarkDoneEventSet();
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
          RequestApplicationStop();
        }
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  model_thread_.join();
  UnloadPresenters(*ui_application_);
  SurfacesShutdownTrace::MarkUnloadPresentersDone();
  SurfacesShutdownTrace::MarkWindowsGone();
  ui_application_ = {};
  ui_domain_.reset();
  if (model_proxy_) {
    session_.presentation_size_queue.UnbindProxy(&*model_proxy_);
  }
  model_proxy_.reset();
  UnregisterSurfacesWin32Classes();
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
  return session_.shutdown_exit_code;
}

}  // namespace apptraverse
