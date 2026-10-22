#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/distill.h"
#include "surfaces_distill.h"

#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/platform/windows/surfaces_win32_z_order.h"
#include "apptraverse/platform/windows/surfaces_win_registration.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/surfaces/surface_button_presenter.h"
#include "apptraverse/ui/ui_presenters.h"
#include "surfaces_demo_win32_registration.h"
#include "apptraverse/surfaces/surface_language_button_presenter.h"
#include "apptraverse/surfaces/surface_window_presenter.h"
#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_registration.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_push_button.h"
#include "surfaces_lifecycle.h"
#include "surfaces_win32_capture.h"
#include "win_app.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

namespace {

void EnsureWin32SmokeRegistration() {
  EnsureObjectRegistration();
  EnsureSurfacesRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();
  EnsureWin32SurfacesPresenterRegistration();
  example::surfaces::EnsureSurfacesDemoWin32Registration();
}

std::filesystem::path AcceptanceScreenshotDir() {
#ifdef APPTRAVERSE_SURFACES_ACCEPTANCE_DIR
  return std::filesystem::path{APPTRAVERSE_SURFACES_ACCEPTANCE_DIR};
#else
  return {};
#endif
}

void SaveAcceptanceScreenshot(HWND hwnd, char const* name) {
  auto const dir = AcceptanceScreenshotDir();
  if (dir.empty() || hwnd == nullptr) {
    return;
  }
  auto const path = dir / (std::string{name} + ".bmp");
  CHECK(SaveWindowClientBmp(hwnd, path));
}

}  // namespace

HWND FindOwned(DWORD pid, wchar_t const* class_name, wchar_t const* title) {
  struct Ctx {
    DWORD pid;
    wchar_t const* class_name;
    wchar_t const* title;
    HWND found;
  } ctx{pid, class_name, title, nullptr};
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        DWORD window_pid = 0;
        GetWindowThreadProcessId(hwnd, &window_pid);
        if (window_pid != c->pid) {
          return TRUE;
        }
        wchar_t name[256]{};
        if (GetClassNameW(hwnd, name, 256) <= 0 ||
            wcscmp(name, c->class_name) != 0) {
          return TRUE;
        }
        if (c->title != nullptr) {
          wchar_t text[256]{};
          GetWindowTextW(hwnd, text, 256);
          if (wcscmp(text, c->title) != 0) {
            return TRUE;
          }
        }
        if (IsWindowVisible(hwnd) == 0) {
          return TRUE;
        }
        c->found = hwnd;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.found;
}

int CountOwnedClass(DWORD pid, wchar_t const* class_name) {
  struct Ctx {
    DWORD pid;
    wchar_t const* class_name;
    int count;
  } ctx{pid, class_name, 0};
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        DWORD window_pid = 0;
        GetWindowThreadProcessId(hwnd, &window_pid);
        if (window_pid != c->pid) {
          return TRUE;
        }
        wchar_t name[256]{};
        if (GetClassNameW(hwnd, name, 256) > 0 &&
            wcscmp(name, c->class_name) == 0 && IsWindowVisible(hwnd) != 0) {
          ++c->count;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.count;
}

void CommitAddLabelSlotUtf8(ae::Domain& domain, Application& application,
                            int slot_index, char const* utf8) {
  auto event = ui::SetUiLocalizationSlotUtf8Event::ptr::Create(
      ae::CreateWith{domain});
  event->slot_index = slot_index;
  event->utf8 = utf8;
  application.label_add->Commit(std::move(event));
}

void CommitLanguageSwitchLabelSlotUtf8(ae::Domain& domain,
                                       Application& application,
                                       int slot_index, char const* utf8) {
  auto event = ui::SetUiLocalizationSlotUtf8Event::ptr::Create(
      ae::CreateWith{domain});
  event->slot_index = slot_index;
  event->utf8 = utf8;
  application.language_switch_label->Commit(std::move(event));
}

void PumpGui(std::chrono::milliseconds slice);

int QueryModelLanguageIndex(WinApp& app) {
  std::promise<int> promise;
  std::future<int> future = promise.get_future();
  CHECK(app.PostModelWork([&](ae::Domain& domain) {
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    promise.set_value(application->locale->language_index());
  }));
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while (future.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
    PumpGui(std::chrono::milliseconds{20});
    CHECK(std::chrono::steady_clock::now() < deadline);
  }
  return future.get();
}

std::wstring WindowText(HWND hwnd);
void PumpGui(std::chrono::milliseconds slice);

bool WaitForWindowEnabled(HWND hwnd, bool enabled,
                          std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if ((IsWindowEnabled(hwnd) != FALSE) == enabled) {
      return true;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  return (IsWindowEnabled(hwnd) != FALSE) == enabled;
}

bool WaitForButtonText(HWND button, std::wstring const& expected,
                       std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (WindowText(button) == expected) {
      return true;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  return WindowText(button) == expected;
}

void PumpGui(std::chrono::milliseconds slice) {
  auto const deadline = std::chrono::steady_clock::now() + slice;
  while (std::chrono::steady_clock::now() < deadline) {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
}

bool WaitOwned(DWORD pid, wchar_t const* class_name, wchar_t const* title,
               HWND* out, std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    HWND hwnd = FindOwned(pid, class_name, title);
    if (hwnd != nullptr) {
      if (out != nullptr) {
        *out = hwnd;
      }
      return true;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  return false;
}

bool WaitCount(DWORD pid, wchar_t const* class_name, int expected,
               std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (CountOwnedClass(pid, class_name) == expected) {
      return true;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  return false;
}

// CountOwnedClass only counts visible windows, and DestroyWindow hides the
// window before IsWindow stops recognising it. Waiting for the count alone
// therefore races the final teardown of the removed HWND.
bool WaitWindowGone(HWND hwnd, std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (IsWindow(hwnd) == 0) {
      return true;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  return false;
}

std::wstring WindowText(HWND hwnd) {
  wchar_t buffer[512]{};
  GetWindowTextW(hwnd, buffer, 512);
  return buffer;
}

std::wstring ControlCaption(HWND hwnd) {
  if (hwnd == nullptr) {
    return {};
  }
  wchar_t buffer[512]{};
  SendMessageW(hwnd, WM_GETTEXT, static_cast<WPARAM>(std::size(buffer)),
               reinterpret_cast<LPARAM>(buffer));
  return buffer;
}

DWORD WindowProcessId(HWND hwnd) {
  DWORD window_pid = 0;
  GetWindowThreadProcessId(hwnd, &window_pid);
  return window_pid;
}

bool ProcessHasExited(HANDLE process, DWORD* exit_code) {
  DWORD code = STILL_ACTIVE;
  if (GetExitCodeProcess(process, &code) == 0) {
    return true;
  }
  if (code != STILL_ACTIVE) {
    if (exit_code != nullptr) {
      *exit_code = code;
    }
    return true;
  }
  return false;
}

bool WaitOwnedChild(DWORD pid, HANDLE process, wchar_t const* class_name,
                    wchar_t const* title, HWND* out,
                    std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    DWORD exit_code = 0;
    if (ProcessHasExited(process, &exit_code)) {
      std::cerr << "load_only process exited early with code " << exit_code
                << " while waiting for a surfaces window\n";
      return false;
    }
    HWND hwnd = FindOwned(pid, class_name, title);
    if (hwnd != nullptr) {
      if (out != nullptr) {
        *out = hwnd;
      }
      return true;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  return false;
}

HWND FindSurfaceButton(HWND surface, int control_id) {
  for (HWND child = nullptr;;) {
    child = FindWindowExW(surface, child, L"BUTTON", nullptr);
    if (child == nullptr) {
      return nullptr;
    }
    if (GetDlgCtrlID(child) == control_id) {
      return child;
    }
  }
}

HWND FindAddButton(HWND surface) {
  return FindSurfaceButton(surface, kSurfaceAddButtonId);
}

HWND FindCloseButton(HWND surface) {
  return FindSurfaceButton(surface, kSurfaceCloseButtonId);
}

HWND FindLanguageButton(HWND surface) {
  return FindSurfaceButton(surface, kSurfaceLanguageButtonId);
}

bool WaitSurfaceChromeReady(HWND surface,
                            std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (FindAddButton(surface) != nullptr &&
        FindCloseButton(surface) != nullptr &&
        FindLanguageButton(surface) != nullptr) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    PumpGui(std::chrono::milliseconds{0});
  }
  return false;
}

void ExpectRuChrome(HWND surface) {
  HWND const add = FindAddButton(surface);
  HWND const close = FindCloseButton(surface);
  HWND const lang = FindLanguageButton(surface);
  CHECK(add != nullptr);
  CHECK(close != nullptr);
  CHECK(lang != nullptr);
  CHECK(GetDlgCtrlID(add) == kSurfaceAddButtonId);
  CHECK(GetDlgCtrlID(close) == kSurfaceCloseButtonId);
  CHECK(GetDlgCtrlID(lang) == kSurfaceLanguageButtonId);
  CHECK(ControlCaption(add) == L"Добавить");
  CHECK(ControlCaption(close) == L"Закрыть это окно");
  CHECK(ControlCaption(lang) == L"EN");
}

void ExpectEnChrome(HWND surface) {
  HWND const add = FindAddButton(surface);
  HWND const close = FindCloseButton(surface);
  HWND const lang = FindLanguageButton(surface);
  CHECK(add != nullptr);
  CHECK(close != nullptr);
  CHECK(lang != nullptr);
  CHECK(ControlCaption(add) == L"Add");
  CHECK(ControlCaption(close) == L"Close this window");
  CHECK(ControlCaption(lang) == L"RU");
}

RECT WindowRect(HWND hwnd) {
  RECT rect{};
  CHECK(GetWindowRect(hwnd, &rect) != 0);
  return rect;
}

bool RectNear(RECT const& a, RECT const& b, int tol) {
  return std::abs(a.left - b.left) <= tol && std::abs(a.top - b.top) <= tol &&
         std::abs((a.right - a.left) - (b.right - b.left)) <= tol &&
         std::abs((a.bottom - a.top) - (b.bottom - b.top)) <= tol;
}

void PlaceWindow(HWND hwnd, int x, int y, int w, int h) {
  CHECK(SetWindowPos(hwnd, nullptr, x, y, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE) != 0);
  PumpGui(std::chrono::milliseconds{50});
}

void TestPresenterHierarchy() {
  EnsureWin32SmokeRegistration();
  auto& registry = ae::Registry::GetRegistry();
  CHECK(registry.GenerationDistance(SurfacePresenter::kClassId,
                                    DesktopSurfacePresenter::kClassId) == 1);
  CHECK(registry.GenerationDistance(DesktopSurfacePresenter::kClassId,
                                    SurfaceWindowPresenter::kClassId) == 1);
  CHECK(registry.GenerationDistance(SurfaceWindowPresenter::kClassId,
                                    Win32SurfaceWindowPresenter::kClassId) == 1);
  CHECK(registry.GenerationDistance(SurfaceButtonPresenter::kClassId,
                                    Win32SurfaceButtonPresenter::kClassId) == 1);
  CHECK(registry.GenerationDistance(SurfaceLanguageButtonPresenter::kClassId,
                                    Win32SurfaceLanguageButtonPresenter::
                                        kClassId) == 1);

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*application);
  Surface& surface = *application->surfaces->surfaces[0];
  CHECK(surface.chrome.add_button->GetClassId() ==
        ui::windows::WinPushButtonPresenter::kClassId);
  CHECK(surface.chrome.language_button->GetClassId() ==
        ui::windows::WinPushButtonPresenter::kClassId);
  CHECK(surface.chrome.close_button->GetClassId() ==
        ui::windows::WinPushButtonPresenter::kClassId);
  auto add = ui::windows::WinPushButtonPresenter::ptr{surface.chrome.add_button};
  CHECK(add);
  CHECK(add->button && add->button->label);
  CHECK(add->window_presenter && add->window_presenter->locale);
  CHECK(add->button->label->Resolve(0) == "Add");
  auto lang = ui::windows::WinPushButtonPresenter::ptr{
      surface.chrome.language_button};
  CHECK(lang);
  CHECK(lang->button && lang->button->label);
  CHECK(&*lang->window_presenter == &*add->window_presenter);
  CHECK(lang->button->label->Resolve(0) == "RU");
  auto close = ui::windows::WinPushButtonPresenter::ptr{
      surface.chrome.close_button};
  CHECK(close);
  CHECK(&*close->window_presenter == &*add->window_presenter);
  CHECK(surface.chrome.add_button->obj_id !=
        surface.chrome.language_button->obj_id);
  CHECK(surface.chrome.add_button->obj_id != surface.chrome.close_button->obj_id);
  CHECK(surface.chrome.language_button->obj_id !=
        surface.chrome.close_button->obj_id);
}

void TestCloseButtonRemovesOne() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_close_btn";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  HWND add1 = FindAddButton(s1);
  HWND close1 = FindCloseButton(s1);
  CHECK(add1 != nullptr);
  CHECK(close1 != nullptr);
  CHECK(app.PostModelWork([&](ae::Domain& domain) {
    auto app_obj = domain.Find(ae::ObjId{surfaces_demo::ToObjId(
        surfaces_demo::ObjId::Application)});
    CHECK(app_obj);
    Surface& surface =
        *static_cast<Application&>(*app_obj).surfaces->surfaces[0];
    CHECK(surface.chrome.add_button->GetClassId() ==
          ui::windows::WinPushButtonPresenter::kClassId);
    CHECK(surface.chrome.language_button->GetClassId() ==
          ui::windows::WinPushButtonPresenter::kClassId);
    CHECK(surface.chrome.close_button->GetClassId() ==
          ui::windows::WinPushButtonPresenter::kClassId);
  }));
  PumpGui(std::chrono::milliseconds{500});
  auto* add_owner =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(add1, GWLP_USERDATA));
  auto* close_owner =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(close1, GWLP_USERDATA));
  CHECK(add_owner != close_owner);
  auto& registry = ae::Registry::GetRegistry();
  CHECK(registry.GenerationDistance(
            ui::windows::WinPushButtonPresenter::kClassId,
            add_owner->GetClassId()) >= 0);
  HWND lang1 = FindLanguageButton(s1);
  CHECK(lang1 != nullptr);
  auto* lang_owner =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(lang1, GWLP_USERDATA));
  CHECK(lang_owner->GetClassId() ==
        ui::windows::WinPushButtonPresenter::kClassId);
  CHECK(close_owner->GetClassId() ==
        ui::windows::WinPushButtonPresenter::kClassId);
  CHECK(GetDlgCtrlID(add1) == kSurfaceAddButtonId);
  CHECK(GetDlgCtrlID(close1) == kSurfaceCloseButtonId);

  SendMessageW(add1, BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  SendMessageW(FindAddButton(s2), BM_CLICK, 0, 0);
  HWND s3 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 3", &s3,
                  std::chrono::seconds{30}));
  CHECK(WaitCount(pid, kSurfacesWindowClass, 3, std::chrono::seconds{10}));

  // Close this window on Surface 2 removes only that Surface.
  SendMessageW(FindCloseButton(s2), BM_CLICK, 0, 0);
  CHECK(WaitCount(pid, kSurfacesWindowClass, 2, std::chrono::seconds{30}));
  CHECK(WaitWindowGone(s2, std::chrono::seconds{30}));
  CHECK(IsWindow(s1) != 0);
  CHECK(IsWindow(s3) != 0);
  CHECK(FindOwned(pid, kSurfacesWindowClass, L"Surface 1") == s1);
  CHECK(FindOwned(pid, kSurfacesWindowClass, L"Surface 3") == s3);

  SendMessageW(FindAddButton(s3), BM_CLICK, 0, 0);
  HWND s4 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 4", &s4,
                  std::chrono::seconds{30}));
  CHECK(WaitCount(pid, kSurfacesWindowClass, 3, std::chrono::seconds{10}));
  CHECK(FindOwned(pid, kSurfacesWindowClass, L"Surface 2") == nullptr);

  // Native X on any window exits the whole app; topology [1,3,4] persists.
  PlaceWindow(s1, 60, 70, 380, 250);
  PlaceWindow(s3, 160, 170, 400, 260);
  PlaceWindow(s4, 260, 270, 420, 270);
  RECT const r1 = WindowRect(s1);
  RECT const r3 = WindowRect(s3);
  RECT const r4 = WindowRect(s4);

  PostMessageW(s3, WM_CLOSE, 0, 0);
  gui.join();
  CHECK(CountOwnedClass(pid, kSurfacesWindowClass) == 0);

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->surfaces.size() == 3);
  CHECK(application->surfaces->surfaces[0]->number == 1);
  CHECK(application->surfaces->surfaces[1]->number == 3);
  CHECK(application->surfaces->surfaces[2]->number == 4);

  WinApp app2;
  std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
  HWND rs1 = nullptr;
  HWND rs3 = nullptr;
  HWND rs4 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &rs1,
                  std::chrono::seconds{30}));
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 3", &rs3,
                  std::chrono::seconds{30}));
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 4", &rs4,
                  std::chrono::seconds{30}));
  CHECK(CountOwnedClass(pid, kSurfacesWindowClass) == 3);
  CHECK(FindOwned(pid, kSurfacesWindowClass, L"Surface 2") == nullptr);
  CHECK(RectNear(WindowRect(rs1), r1, 2));
  CHECK(RectNear(WindowRect(rs3), r3, 2));
  CHECK(RectNear(WindowRect(rs4), r4, 2));

  PostMessageW(rs1, WM_CLOSE, 0, 0);
  gui2.join();
  std::filesystem::remove_all(dir);
}

void TestNativeXKeepsAllSurfaces() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_native_x";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  SendMessageW(FindAddButton(s2), BM_CLICK, 0, 0);
  HWND s3 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 3", &s3,
                  std::chrono::seconds{30}));
  CHECK(WaitCount(pid, kSurfacesWindowClass, 3, std::chrono::seconds{10}));

  PlaceWindow(s1, 50, 60, 370, 240);
  PlaceWindow(s2, 150, 160, 390, 250);
  PlaceWindow(s3, 250, 260, 410, 260);
  RECT const r1 = WindowRect(s1);
  RECT const r2 = WindowRect(s2);
  RECT const r3 = WindowRect(s3);

  // X on non-last window exits everything; all three Surfaces persist.
  PostMessageW(s2, WM_CLOSE, 0, 0);
  gui.join();
  CHECK(CountOwnedClass(pid, kSurfacesWindowClass) == 0);

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->surfaces.size() == 3);
  CHECK(application->surfaces->surfaces[0]->number == 1);
  CHECK(application->surfaces->surfaces[1]->number == 2);
  CHECK(application->surfaces->surfaces[2]->number == 3);

  WinApp app2;
  std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
  HWND rs1 = nullptr;
  HWND rs2 = nullptr;
  HWND rs3 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &rs1,
                  std::chrono::seconds{30}));
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &rs2,
                  std::chrono::seconds{30}));
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 3", &rs3,
                  std::chrono::seconds{30}));
  CHECK(CountOwnedClass(pid, kSurfacesWindowClass) == 3);
  CHECK(RectNear(WindowRect(rs1), r1, 2));
  CHECK(RectNear(WindowRect(rs2), r2, 2));
  CHECK(RectNear(WindowRect(rs3), r3, 2));

  // Last Close this window exits without Remove.
  SendMessageW(FindCloseButton(rs2), BM_CLICK, 0, 0);
  CHECK(WaitCount(pid, kSurfacesWindowClass, 2, std::chrono::seconds{30}));
  SendMessageW(FindCloseButton(rs1), BM_CLICK, 0, 0);
  CHECK(WaitCount(pid, kSurfacesWindowClass, 1, std::chrono::seconds{30}));
  HWND last = FindOwned(pid, kSurfacesWindowClass, L"Surface 3");
  CHECK(last != nullptr);
  SendMessageW(FindCloseButton(last), BM_CLICK, 0, 0);
  gui2.join();

  DirectoryDomainStorage storage2{dir};
  ae::Domain domain2{storage2};
  auto application2 = LoadApplication<Application>(
      domain2, ae::ObjId{surfaces_demo::ToObjId(
                   surfaces_demo::ObjId::Application)});
  CHECK(application2->surfaces->surfaces.size() == 1);
  CHECK(application2->surfaces->surfaces[0]->number == 3);

  std::filesystem::remove_all(dir);
}

void ApplySurfaceNumbersZOrder(DWORD pid,
                               std::vector<std::uint32_t> top_to_bottom) {
  std::vector<HWND> hwnds;
  hwnds.reserve(top_to_bottom.size());
  for (std::uint32_t number : top_to_bottom) {
    wchar_t title[64]{};
    swprintf_s(title, L"Surface %u", number);
    HWND const hwnd = FindOwned(pid, kSurfacesWindowClass, title);
    CHECK(hwnd != nullptr);
    hwnds.push_back(hwnd);
  }
  ApplySurfaceHostWindowsZOrderTopToBottom(hwnds);
  PumpGui(std::chrono::milliseconds{100});
}

void ActivateSurfaceNumber(DWORD pid, std::uint32_t number) {
  wchar_t title[64]{};
  swprintf_s(title, L"Surface %u", number);
  HWND const hwnd = FindOwned(pid, kSurfacesWindowClass, title);
  CHECK(hwnd != nullptr);
  CHECK(SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE) != 0);
  SendMessageW(hwnd, WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, FALSE), 0);
  PumpGui(std::chrono::milliseconds{200});
}

HWND TopSurfacesWindow(DWORD pid) {
  struct Ctx {
    DWORD pid;
    HWND top;
  } ctx{pid, nullptr};
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        DWORD window_pid = 0;
        GetWindowThreadProcessId(hwnd, &window_pid);
        if (window_pid != c->pid || IsWindowVisible(hwnd) == 0) {
          return TRUE;
        }
        wchar_t name[256]{};
        if (GetClassNameW(hwnd, name, 256) <= 0 ||
            wcscmp(name, kSurfacesWindowClass) != 0) {
          return TRUE;
        }
        c->top = hwnd;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.top;
}

void TestActiveZOrderRestored() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_zorder";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();

  std::vector<std::uint32_t> const expected_stack{2, 3, 1};
  std::cout << "zorder_test before_close stack expect:";
  for (auto n : expected_stack) {
    std::cout << ' ' << n;
  }
  std::cout << '\n';

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  SendMessageW(FindAddButton(s2), BM_CLICK, 0, 0);
  HWND s3 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 3", &s3,
                  std::chrono::seconds{30}));
  CHECK(WaitCount(pid, kSurfacesWindowClass, 3, std::chrono::seconds{10}));

  ApplySurfaceNumbersZOrder(pid, expected_stack);
  ActivateSurfaceNumber(pid, 2);
  CHECK(SurfaceNumbersFromHostWindowsTopToBottom(pid) == expected_stack);

  PostMessageW(s2, WM_CLOSE, 0, 0);
  gui.join();
  CHECK(CountOwnedClass(pid, kSurfacesWindowClass) == 0);

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->mobile_current);
  CHECK(application->surfaces->mobile_current->number == 2);
  CHECK(application->surfaces->desktop_z_order.size() == 3);
  CHECK(application->surfaces->desktop_z_order[0]->number == 2);
  CHECK(application->surfaces->desktop_z_order[1]->number == 3);
  CHECK(application->surfaces->desktop_z_order[2]->number == 1);

  WinApp app2;
  std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 3", &s3,
                  std::chrono::seconds{30}));
  PumpGui(std::chrono::milliseconds{200});
  auto const restored = SurfaceNumbersFromHostWindowsTopToBottom(pid);
  std::cout << "zorder_test after_load stack:";
  for (auto n : restored) {
    std::cout << ' ' << n;
  }
  std::cout << '\n';
  CHECK(restored == expected_stack);
  CHECK(TopSurfacesWindow(pid) == s2);

  PostMessageW(s2, WM_CLOSE, 0, 0);
  gui2.join();
  std::filesystem::remove_all(dir);
}

void TestFullDesktopZOrderDistinctStacksSameTop() {
  DWORD const pid = GetCurrentProcessId();
  struct Scenario {
    char const* name;
    std::vector<std::uint32_t> stack;
  };
  Scenario const scenarios[] = {
      {"same_top_a", {2, 4, 1, 3}},
      {"same_top_b", {2, 3, 4, 1}},
  };

  for (Scenario const& scenario : scenarios) {
    auto dir = std::filesystem::temp_directory_path() /
               ("apptraverse_surfaces_win32_zorder_" + std::string{scenario.name});
    std::filesystem::remove_all(dir);

    WinApp app;
    std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

    HWND s1 = nullptr;
    CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                    std::chrono::seconds{30}));
    for (int i = 0; i < 3; ++i) {
      SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
      PumpGui(std::chrono::milliseconds{300});
    }
    CHECK(WaitCount(pid, kSurfacesWindowClass, 4, std::chrono::seconds{30}));

    ApplySurfaceNumbersZOrder(pid, scenario.stack);
    ActivateSurfaceNumber(pid, 2);
    CHECK(SurfaceNumbersFromHostWindowsTopToBottom(pid) == scenario.stack);

    std::cout << "zorder_test " << scenario.name << " before_close:";
    for (auto n : scenario.stack) {
      std::cout << ' ' << n;
    }
    std::cout << '\n';

    HWND const close_before_save =
        FindOwned(pid, kSurfacesWindowClass, L"Surface 2");
    PostMessageW(close_before_save, WM_CLOSE, 0, 0);
    gui.join();

    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    CHECK(application->surfaces->desktop_z_order.size() == 4);
    for (std::size_t i = 0; i < scenario.stack.size(); ++i) {
      CHECK(application->surfaces->desktop_z_order[i]->number ==
            scenario.stack[i]);
    }

    WinApp app2;
    std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
    CHECK(WaitCount(pid, kSurfacesWindowClass, 4, std::chrono::seconds{30}));
    PumpGui(std::chrono::milliseconds{300});
    auto const restored = SurfaceNumbersFromHostWindowsTopToBottom(pid);
    std::cout << "zorder_test " << scenario.name << " after_load:";
    for (auto n : restored) {
      std::cout << ' ' << n;
    }
    std::cout << '\n';
    CHECK(restored == scenario.stack);

    HWND const close_after_load =
        FindOwned(pid, kSurfacesWindowClass, L"Surface 2");
    PostMessageW(close_after_load, WM_CLOSE, 0, 0);
    gui2.join();
    std::filesystem::remove_all(dir);
  }
}

void TestDesktopZOrderAfterRemoveMiddleAndAdd() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_zorder_remove_add";
  std::filesystem::remove_all(dir);

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
  SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
  CHECK(WaitCount(pid, kSurfacesWindowClass, 3, std::chrono::seconds{30}));

  HWND s2 = FindOwned(pid, kSurfacesWindowClass, L"Surface 2");
  SendMessageW(FindCloseButton(s2), BM_CLICK, 0, 0);
  PumpGui(std::chrono::milliseconds{500});
  CHECK(WaitCount(pid, kSurfacesWindowClass, 2, std::chrono::seconds{30}));

  s1 = FindOwned(pid, kSurfacesWindowClass, L"Surface 1");
  SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
  CHECK(WaitCount(pid, kSurfacesWindowClass, 3, std::chrono::seconds{30}));

  std::vector<std::uint32_t> const expected_stack{3, 1, 4};
  ApplySurfaceNumbersZOrder(pid, expected_stack);
  ActivateSurfaceNumber(pid, 3);
  CHECK(SurfaceNumbersFromHostWindowsTopToBottom(pid) == expected_stack);

  HWND const close_before_save =
      FindOwned(pid, kSurfacesWindowClass, L"Surface 1");
  PostMessageW(close_before_save, WM_CLOSE, 0, 0);
  gui.join();

  WinApp app2;
  std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
  CHECK(WaitCount(pid, kSurfacesWindowClass, 3, std::chrono::seconds{30}));
  PumpGui(std::chrono::milliseconds{300});
  CHECK(SurfaceNumbersFromHostWindowsTopToBottom(pid) == expected_stack);
  HWND const close_after_load =
      FindOwned(pid, kSurfacesWindowClass, L"Surface 1");
  PostMessageW(close_after_load, WM_CLOSE, 0, 0);
  gui2.join();
  std::filesystem::remove_all(dir);
}

void PlaceClientSize(HWND hwnd, int client_w, int client_h) {
  RECT desired{0, 0, client_w, client_h};
  auto const style =
      static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
  auto const ex_style =
      static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
  CHECK(AdjustWindowRectEx(&desired, style, FALSE, ex_style) != 0);
  RECT window{};
  CHECK(GetWindowRect(hwnd, &window) != 0);
  PlaceWindow(hwnd, window.left, window.top, desired.right - desired.left,
              desired.bottom - desired.top);
  RECT client{};
  CHECK(GetClientRect(hwnd, &client) != 0);
  int const width = client.right - client.left;
  int const height = client.bottom - client.top;
  if (client_w >= client_h) {
    CHECK(width >= height);
  } else {
    CHECK(width < height);
  }
}

RECT ScreenRect(HWND hwnd) {
  RECT rect{};
  CHECK(GetWindowRect(hwnd, &rect) != 0);
  return rect;
}

bool ButtonsHorizontal(HWND surface) {
  RECT const add = ScreenRect(FindAddButton(surface));
  RECT const lang = ScreenRect(FindLanguageButton(surface));
  RECT const close = ScreenRect(FindCloseButton(surface));
  return add.left < lang.left && lang.left < close.left &&
         std::abs(add.top - lang.top) <= 2 &&
         std::abs(lang.top - close.top) <= 2;
}

bool ButtonsVertical(HWND surface) {
  RECT const add = ScreenRect(FindAddButton(surface));
  RECT const lang = ScreenRect(FindLanguageButton(surface));
  RECT const close = ScreenRect(FindCloseButton(surface));
  return add.top < lang.top && lang.top < close.top &&
         std::abs(add.left - lang.left) <= 2 &&
         std::abs(lang.left - close.left) <= 2;
}

void WaitHorizontalButtons(HWND surface) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (std::chrono::steady_clock::now() < deadline) {
    if (ButtonsHorizontal(surface)) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  CHECK(false && "buttons did not become horizontal after model publication");
}

void WaitVerticalButtons(HWND surface) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (std::chrono::steady_clock::now() < deadline) {
    if (ButtonsVertical(surface)) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  CHECK(false && "buttons did not become vertical after model publication");
}

void TestControlsFollowPresentationSize() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_presentation_size";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  CHECK(FindAddButton(s1) != nullptr);
  CHECK(FindCloseButton(s1) != nullptr);

  PlaceClientSize(s1, 800, 400);
  WaitHorizontalButtons(s1);
  SaveAcceptanceScreenshot(s1, "layout_wide_three_buttons");

  PlaceClientSize(s1, 400, 800);
  WaitVerticalButtons(s1);
  SaveAcceptanceScreenshot(s1, "layout_narrow_three_buttons");

  // Square is wide under presentation_width >= presentation_height.
  PlaceClientSize(s1, 500, 500);
  WaitHorizontalButtons(s1);

  PlaceClientSize(s1, 700, 300);
  WaitHorizontalButtons(s1);

  SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  PlaceClientSize(s2, 360, 640);
  WaitVerticalButtons(s2);
  SendMessageW(FindCloseButton(s2), BM_CLICK, 0, 0);
  CHECK(WaitCount(pid, kSurfacesWindowClass, 1, std::chrono::seconds{30}));
  CHECK(IsWindow(s1) != 0);

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  Surface& surface = *application->surfaces->surfaces[0];
  CHECK(surface.presentation_width == 700);
  CHECK(surface.presentation_height == 300);
  bool saw_size_event = false;
  for (auto const& entry : surface.journal) {
    if (entry.event->GetClassId() ==
        SurfacePresentationSizeChangedEvent::kClassId) {
      saw_size_event = true;
      break;
    }
  }
  CHECK(saw_size_event);

  std::filesystem::remove_all(dir);
}

void TestPostReleaseOuterFrameStable() {
  if (std::getenv("APPTRAVERSE_SURFACES_LONG_ACCEPTANCE") == nullptr) {
    return;
  }
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_post_release_frame";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));

  auto const resize_until =
      std::chrono::steady_clock::now() + std::chrono::seconds{60};
  int i = 0;
  while (std::chrono::steady_clock::now() < resize_until) {
    if (i % 2 == 0) {
      PlaceClientSize(s1, 820, 380 + (i % 30));
    } else {
      PlaceClientSize(s1, 380 + (i % 30), 820);
    }
    ++i;
    PumpGui(std::chrono::milliseconds{5});
  }

  RECT outer_at_release = WindowRect(s1);
  RECT client_at_release{};
  CHECK(GetClientRect(s1, &client_at_release) != 0);
  bool layout_horizontal = ButtonsHorizontal(s1);

  int outer_changes = 0;
  int client_changes = 0;
  int layout_flips = 0;
  auto const observe_until =
      std::chrono::steady_clock::now() + std::chrono::seconds{45};
  while (std::chrono::steady_clock::now() < observe_until) {
    PumpGui(std::chrono::milliseconds{50});
    RECT outer_now = WindowRect(s1);
    RECT client_now{};
    CHECK(GetClientRect(s1, &client_now) != 0);
    if (!RectNear(outer_at_release, outer_now, 1)) {
      ++outer_changes;
      outer_at_release = outer_now;
    }
    if (!RectNear(client_at_release, client_now, 1)) {
      ++client_changes;
      client_at_release = client_now;
    }
    bool const horiz = ButtonsHorizontal(s1);
    if (horiz != layout_horizontal) {
      ++layout_flips;
      layout_horizontal = horiz;
    }
  }

  std::cout << "post_release outer_rect_changes=" << outer_changes
            << " client_rect_changes=" << client_changes
            << " layout_flips=" << layout_flips << "\n";
  CHECK(outer_changes == 0);

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();
  std::filesystem::remove_all(dir);
}

void TestDynamicAddLibraryPushButtonNotifierAndEnabled() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_dynamic_add_notifier";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  HWND add2 = FindAddButton(s2);
  CHECK(add2 != nullptr);
  CHECK(IsWindowEnabled(add2) != FALSE);

  std::promise<bool> notifier_promise;
  std::future<bool> notifier_future = notifier_promise.get_future();
  CHECK(app.PostModelWork([&](ae::Domain& domain) {
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    CHECK(application->surfaces->surfaces.size() == 2);
    Surface& surface2 = *application->surfaces->surfaces[1];
    auto add_presenter =
        ui::UiPushButtonPresenter::ptr{surface2.chrome.add_button};
    CHECK(add_presenter);
    CHECK(add_presenter->button);
    notifier_promise.set_value(
        add_presenter->button->HasMaterializedChangeNotifier());
  }));
  {
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (notifier_future.wait_for(std::chrono::milliseconds{0}) !=
           std::future_status::ready) {
      PumpGui(std::chrono::milliseconds{20});
      CHECK(std::chrono::steady_clock::now() < deadline);
    }
  }
  CHECK(notifier_future.get());

  CHECK(app.PostModelWork([&](ae::Domain& domain) {
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    Surface& surface2 = *application->surfaces->surfaces[1];
    auto add_presenter =
        ui::UiPushButtonPresenter::ptr{surface2.chrome.add_button};
    auto disable = ui::SetUiPushButtonEnabledEvent::ptr::Create(
        ae::CreateWith{domain});
    disable->enabled = false;
    add_presenter->button->Commit(std::move(disable));
  }));
  CHECK(WaitForWindowEnabled(add2, false, std::chrono::seconds{30}));

  CHECK(app.PostModelWork([&](ae::Domain& domain) {
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    Surface& surface2 = *application->surfaces->surfaces[1];
    auto add_presenter =
        ui::UiPushButtonPresenter::ptr{surface2.chrome.add_button};
    auto enable = ui::SetUiPushButtonEnabledEvent::ptr::Create(
        ae::CreateWith{domain});
    enable->enabled = true;
    add_presenter->button->Commit(std::move(enable));
  }));
  CHECK(WaitForWindowEnabled(add2, true, std::chrono::seconds{30}));

  SendMessageW(add2, BM_CLICK, 0, 0);
  HWND s3 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 3", &s3,
                  std::chrono::seconds{30}));
  CHECK(WaitCount(pid, kSurfacesWindowClass, 3, std::chrono::seconds{10}));

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();
  std::filesystem::remove_all(dir);
}

void TestLibraryLanguageSingleModelToggle() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_language_toggle";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();
  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  CHECK(WaitSurfaceChromeReady(s1, std::chrono::seconds{30}));
  HWND lang1 = FindLanguageButton(s1);

  auto check_model_lazy_locale = [&](int active_lang) {
    int const inactive_lang = 1 - active_lang;
    std::promise<void> promise;
    std::future<void> future = promise.get_future();
    CHECK(app.PostModelWork([&](ae::Domain& domain) {
      auto application = LoadApplication<Application>(
          domain, ae::ObjId{surfaces_demo::ToObjId(
                      surfaces_demo::ObjId::Application)});
      CHECK(application->locale->language_index() == active_lang);
      ui::UiLocalizationString* localized[] = {
          &*application->label_add,
          &*application->label_close,
          &*application->window_title_prefix,
          &*application->language_switch_label,
      };
      for (ui::UiLocalizationString* loc : localized) {
        CHECK(loc->strings.size() >= 2);
        auto const active_slot = static_cast<std::size_t>(active_lang);
        auto const inactive_slot = static_cast<std::size_t>(inactive_lang);
        CHECK(loc->strings[active_slot].is_loaded());
        CHECK(!loc->strings[inactive_slot].is_loaded());
        ae::ObjId const inactive_id = loc->strings[inactive_slot].id();
        CHECK(!domain.Find(inactive_id));
      }
      promise.set_value();
    }));
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (future.wait_for(std::chrono::milliseconds{0}) !=
           std::future_status::ready) {
      PumpGui(std::chrono::milliseconds{20});
      CHECK(std::chrono::steady_clock::now() < deadline);
    }
  };

  CHECK(QueryModelLanguageIndex(app) == 0);
  CHECK(WaitForButtonText(lang1, L"RU", std::chrono::seconds{10}));
  ExpectEnChrome(s1);
  CHECK(WindowText(s1) == L"Surface 1");
  check_model_lazy_locale(0);

  SendMessageW(lang1, BM_CLICK, 0, 0);
  CHECK(WaitForButtonText(lang1, L"EN", std::chrono::seconds{10}));
  CHECK(WindowText(s1) == L"Поверхность 1");
  ExpectRuChrome(s1);
  CHECK(QueryModelLanguageIndex(app) == 1);
  check_model_lazy_locale(1);

  SendMessageW(lang1, BM_CLICK, 0, 0);
  CHECK(WaitForButtonText(lang1, L"RU", std::chrono::seconds{10}));
  ExpectEnChrome(s1);
  CHECK(WindowText(s1) == L"Surface 1");
  CHECK(QueryModelLanguageIndex(app) == 0);
  check_model_lazy_locale(0);

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();
  std::filesystem::remove_all(dir);
}

void TestLibraryLanguageCustomLabelFreshProfile() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_language_custom_fresh";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();
  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto application = BuildSurfacesGraph(domain);
    FinalizeDistilledGraph(*application);
    CommitLanguageSwitchLabelSlotUtf8(domain, *application, 0, "Switch");
    CommitLanguageSwitchLabelSlotUtf8(domain, *application, 1, "Язык");
    SaveDistilledRoot(*application);
  }

  DWORD const pid = GetCurrentProcessId();
  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};
  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  HWND lang1 = FindLanguageButton(s1);
  CHECK(WaitForButtonText(lang1, L"Switch", std::chrono::seconds{10}));
  SendMessageW(lang1, BM_CLICK, 0, 0);
  CHECK(WaitForButtonText(lang1, L"Язык", std::chrono::seconds{10}));

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();
  std::filesystem::remove_all(dir);
}

void TestLanguageLayoutOrientationPreservation() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_language_layout";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();
  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  PlaceClientSize(s1, 800, 400);
  WaitHorizontalButtons(s1);
  RECT const outer_at_start = WindowRect(s1);
  RECT const add_at_start = ScreenRect(FindAddButton(s1));
  RECT const lang_at_start = ScreenRect(FindLanguageButton(s1));
  RECT const close_at_start = ScreenRect(FindCloseButton(s1));

  PlaceClientSize(s1, 400, 800);
  WaitVerticalButtons(s1);

  PlaceClientSize(s1, 800, 400);
  WaitHorizontalButtons(s1);
  CHECK(RectNear(WindowRect(s1), outer_at_start, 2));
  CHECK(RectNear(ScreenRect(FindAddButton(s1)), add_at_start, 2));
  CHECK(RectNear(ScreenRect(FindLanguageButton(s1)), lang_at_start, 2));
  CHECK(RectNear(ScreenRect(FindCloseButton(s1)), close_at_start, 2));

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();
  std::filesystem::remove_all(dir);
}

void TestDynamicLanguageLibraryPushButtonNotifierAndEnabled() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_dynamic_language_notifier";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();
  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  HWND lang2 = FindLanguageButton(s2);
  CHECK(lang2 != nullptr);

  std::promise<bool> notifier_promise;
  std::future<bool> notifier_future = notifier_promise.get_future();
  CHECK(app.PostModelWork([&](ae::Domain& domain) {
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    Surface& surface2 = *application->surfaces->surfaces[1];
    auto lang_presenter =
        ui::UiPushButtonPresenter::ptr{surface2.chrome.language_button};
    CHECK(lang_presenter && lang_presenter->button);
    notifier_promise.set_value(
        lang_presenter->button->HasMaterializedChangeNotifier());
  }));
  {
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (notifier_future.wait_for(std::chrono::milliseconds{0}) !=
           std::future_status::ready) {
      PumpGui(std::chrono::milliseconds{20});
      CHECK(std::chrono::steady_clock::now() < deadline);
    }
  }
  CHECK(notifier_future.get());

  int const index_before = QueryModelLanguageIndex(app);
  CHECK(app.PostModelWork([&](ae::Domain& domain) {
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    Surface& surface2 = *application->surfaces->surfaces[1];
    auto lang_presenter =
        ui::UiPushButtonPresenter::ptr{surface2.chrome.language_button};
    auto disable = ui::SetUiPushButtonEnabledEvent::ptr::Create(
        ae::CreateWith{domain});
    disable->enabled = false;
    lang_presenter->button->Commit(std::move(disable));
  }));
  CHECK(WaitForWindowEnabled(lang2, false, std::chrono::seconds{30}));
  SendMessageW(lang2, BM_CLICK, 0, 0);
  PumpGui(std::chrono::milliseconds{500});
  CHECK(QueryModelLanguageIndex(app) == index_before);

  CHECK(app.PostModelWork([&](ae::Domain& domain) {
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    Surface& surface2 = *application->surfaces->surfaces[1];
    auto lang_presenter =
        ui::UiPushButtonPresenter::ptr{surface2.chrome.language_button};
    auto enable = ui::SetUiPushButtonEnabledEvent::ptr::Create(
        ae::CreateWith{domain});
    enable->enabled = true;
    lang_presenter->button->Commit(std::move(enable));
  }));
  CHECK(WaitForWindowEnabled(lang2, true, std::chrono::seconds{30}));
  SendMessageW(lang2, BM_CLICK, 0, 0);
  CHECK(WaitForButtonText(lang2, L"EN", std::chrono::seconds{10}));
  CHECK(QueryModelLanguageIndex(app) == 1 - index_before);

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();
  std::filesystem::remove_all(dir);
}

void TestPerWindowCloseChildCommandHandlerRouting() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_close_handler_routing";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();
  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));

  SendMessageW(
      s1, WM_COMMAND,
      MAKEWPARAM(static_cast<WPARAM>(kSurfaceCloseButtonId), BN_CLICKED),
      reinterpret_cast<LPARAM>(FindAddButton(s1)));
  PumpGui(std::chrono::milliseconds{200});
  CHECK(WaitCount(pid, kSurfacesWindowClass, 2, std::chrono::seconds{5}));

  SendMessageW(
      s1, WM_COMMAND,
      MAKEWPARAM(static_cast<WPARAM>(kSurfaceAddButtonId), BN_CLICKED),
      reinterpret_cast<LPARAM>(FindCloseButton(s1)));
  PumpGui(std::chrono::milliseconds{200});
  CHECK(WaitCount(pid, kSurfacesWindowClass, 2, std::chrono::seconds{5}));

  SendMessageW(FindCloseButton(s2), BM_CLICK, 0, 0);
  CHECK(WaitCount(pid, kSurfacesWindowClass, 1, std::chrono::seconds{30}));

  SendMessageW(FindCloseButton(s1), BM_CLICK, 0, 0);
  gui.join();
  std::filesystem::remove_all(dir);
}

void TestDynamicCloseLibraryPushButtonNotifierAndEnabled() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_dynamic_close_notifier";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();
  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  HWND close2 = FindCloseButton(s2);
  CHECK(close2 != nullptr);

  std::promise<bool> notifier_promise;
  std::future<bool> notifier_future = notifier_promise.get_future();
  CHECK(app.PostModelWork([&](ae::Domain& domain) {
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    Surface& surface2 = *application->surfaces->surfaces[1];
    auto close_presenter =
        ui::UiPushButtonPresenter::ptr{surface2.chrome.close_button};
    CHECK(close_presenter && close_presenter->button);
    notifier_promise.set_value(
        close_presenter->button->HasMaterializedChangeNotifier());
  }));
  {
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (notifier_future.wait_for(std::chrono::milliseconds{0}) !=
           std::future_status::ready) {
      PumpGui(std::chrono::milliseconds{20});
      CHECK(std::chrono::steady_clock::now() < deadline);
    }
  }
  CHECK(notifier_future.get());

  CHECK(app.PostModelWork([&](ae::Domain& domain) {
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    Surface& surface2 = *application->surfaces->surfaces[1];
    auto close_presenter =
        ui::UiPushButtonPresenter::ptr{surface2.chrome.close_button};
    auto disable = ui::SetUiPushButtonEnabledEvent::ptr::Create(
        ae::CreateWith{domain});
    disable->enabled = false;
    close_presenter->button->Commit(std::move(disable));
  }));
  CHECK(WaitForWindowEnabled(close2, false, std::chrono::seconds{30}));
  SendMessageW(close2, BM_CLICK, 0, 0);
  PumpGui(std::chrono::milliseconds{500});
  CHECK(WaitCount(pid, kSurfacesWindowClass, 2, std::chrono::seconds{5}));

  CHECK(app.PostModelWork([&](ae::Domain& domain) {
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    Surface& surface2 = *application->surfaces->surfaces[1];
    auto close_presenter =
        ui::UiPushButtonPresenter::ptr{surface2.chrome.close_button};
    auto enable = ui::SetUiPushButtonEnabledEvent::ptr::Create(
        ae::CreateWith{domain});
    enable->enabled = true;
    close_presenter->button->Commit(std::move(enable));
  }));
  CHECK(WaitForWindowEnabled(close2, true, std::chrono::seconds{30}));
  SendMessageW(close2, BM_CLICK, 0, 0);
  CHECK(WaitCount(pid, kSurfacesWindowClass, 1, std::chrono::seconds{30}));

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();
  std::filesystem::remove_all(dir);
}

void TestLibraryAddCustomLabelFreshProfile() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_add_custom_fresh";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();
  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto application = BuildSurfacesGraph(domain);
    FinalizeDistilledGraph(*application);
    CommitAddLabelSlotUtf8(domain, *application, 0, "Create window");
    CommitAddLabelSlotUtf8(domain, *application, 1, "Создать окно");
    SaveDistilledRoot(*application);
  }

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  HWND add1 = FindAddButton(s1);
  CHECK(add1 != nullptr);
  CHECK(WaitForButtonText(add1, L"Create window", std::chrono::seconds{10}));

  SendMessageW(add1, BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  CHECK(WaitForButtonText(FindAddButton(s2), L"Create window",
                          std::chrono::seconds{10}));

  SendMessageW(FindLanguageButton(s1), BM_CLICK, 0, 0);
  PumpGui(std::chrono::milliseconds{500});
  CHECK(WaitForButtonText(add1, L"Создать окно", std::chrono::seconds{10}));
  CHECK(WaitForButtonText(FindAddButton(s2), L"Создать окно",
                          std::chrono::seconds{10}));

  SendMessageW(FindLanguageButton(s2), BM_CLICK, 0, 0);
  PumpGui(std::chrono::milliseconds{500});
  CHECK(WaitForButtonText(FindAddButton(s2), L"Create window",
                          std::chrono::seconds{10}));

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();
  std::filesystem::remove_all(dir);
}

void TestLibraryAddCustomLabelModelEvent() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_add_custom_live";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  HWND add1 = FindAddButton(s1);
  CHECK(add1 != nullptr);
  CHECK(WaitForButtonText(add1, L"Add", std::chrono::seconds{10}));

  CHECK(app.PostModelWork([&](ae::Domain& domain) {
    auto loaded = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    CommitAddLabelSlotUtf8(domain, *loaded, 0, "Create window");
    CommitAddLabelSlotUtf8(domain, *loaded, 1, "Создать окно");
  }));
  CHECK(WaitForButtonText(add1, L"Create window", std::chrono::seconds{30}));

  SendMessageW(FindLanguageButton(s1), BM_CLICK, 0, 0);
  CHECK(WaitForButtonText(add1, L"Создать окно", std::chrono::seconds{10}));

  SendMessageW(FindLanguageButton(s1), BM_CLICK, 0, 0);
  CHECK(WaitForButtonText(add1, L"Create window", std::chrono::seconds{10}));

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();
  std::filesystem::remove_all(dir);
}

void TestLocalizationEnRuEn() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_locale";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  HWND add1 = FindAddButton(s1);
  HWND lang1 = FindLanguageButton(s1);
  HWND close1 = FindCloseButton(s1);
  CHECK(add1 != nullptr);
  CHECK(lang1 != nullptr);
  CHECK(close1 != nullptr);
  CHECK(WindowText(s1) == L"Surface 1");
  CHECK(WindowText(add1) == L"Add");
  CHECK(WindowText(lang1) == L"RU");
  CHECK(WindowText(close1) == L"Close this window");

  SendMessageW(add1, BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  HWND const add1_before = add1;
  HWND const lang1_before = lang1;
  HWND const close1_before = close1;
  HWND const s1_before = s1;

  SendMessageW(lang1, BM_CLICK, 0, 0);
  PumpGui(std::chrono::milliseconds{500});
  CHECK(WindowText(s1) == L"Поверхность 1");
  CHECK(WindowText(add1) == L"Добавить");
  CHECK(WindowText(lang1) == L"EN");
  CHECK(WindowText(close1) == L"Закрыть это окно");
  CHECK(WindowText(FindAddButton(s2)) == L"Добавить");
  SaveAcceptanceScreenshot(s1, "ru_wide_surface1");
  SaveAcceptanceScreenshot(s2, "ru_wide_surface2");
  CHECK(add1 == add1_before);
  CHECK(lang1 == lang1_before);
  CHECK(close1 == close1_before);
  CHECK(s1 == s1_before);

  SendMessageW(FindLanguageButton(s2), BM_CLICK, 0, 0);
  PumpGui(std::chrono::milliseconds{500});
  CHECK(WindowText(s1) == L"Surface 1");
  CHECK(WindowText(s2) == L"Surface 2");
  CHECK(WindowText(FindAddButton(s2)) == L"Add");

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->locale->language_index() == 0);

  std::filesystem::remove_all(dir);
}

void TestLoadOnlyRestoresRuProfile() {
#ifdef APPTRAVERSE_SURFACES_LOAD_ONLY_EXE
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_load_only_acceptance";
  std::filesystem::remove_all(dir);

  EnsureWin32SmokeRegistration();

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  SendMessageW(FindLanguageButton(s1), BM_CLICK, 0, 0);
  PumpGui(std::chrono::milliseconds{500});
  CHECK(WindowText(s1) == L"Поверхность 1");
  PlaceWindow(s1, 80, 90, 400, 280);
  PlaceWindow(s2, 200, 210, 420, 300);
  CHECK(SetWindowPos(s2, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0);
  SendMessageW(s2, WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, FALSE), 0);
  PumpGui(std::chrono::milliseconds{200});
  PumpGui(std::chrono::seconds{2});

  RECT const saved_r1 = WindowRect(s1);
  RECT const saved_r2 = WindowRect(s2);
  CHECK(TopSurfacesWindow(pid) == s2);

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();

  DirectoryDomainStorage probe{dir};
  ae::Domain probe_domain{probe};
  auto saved = LoadApplication<Application>(
      probe_domain, ae::ObjId{surfaces_demo::ToObjId(
                        surfaces_demo::ObjId::Application)});
  CHECK(saved->locale->language_index() == 1);

  std::filesystem::path const load_only_path{
      APPTRAVERSE_SURFACES_LOAD_ONLY_EXE};
  std::cout << "load_only exe: " << load_only_path << '\n';
#ifdef NDEBUG
  std::cout << "smoke config: Release\n";
#else
  std::cout << "smoke config: Debug\n";
#endif

  std::wstring cmdline = L"\"" + load_only_path.wstring() +
                         L"\" --state-dir \"" + dir.wstring() + L"\"";
  std::vector<wchar_t> cmd_storage(cmdline.begin(), cmdline.end());
  cmd_storage.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  CHECK(CreateProcessW(nullptr, cmd_storage.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &si, &pi) != 0);
  CloseHandle(pi.hThread);
  DWORD const load_only_pid = pi.dwProcessId;

  HWND rs1 = nullptr;
  HWND rs2 = nullptr;
  CHECK(WaitOwnedChild(load_only_pid, pi.hProcess, kSurfacesWindowClass,
                       L"Поверхность 1", &rs1, std::chrono::seconds{30}));
  CHECK(WaitOwnedChild(load_only_pid, pi.hProcess, kSurfacesWindowClass,
                       L"Поверхность 2", &rs2, std::chrono::seconds{30}));
  CHECK(rs1 != rs2);
  CHECK(IsWindow(rs1) != 0);
  CHECK(IsWindow(rs2) != 0);
  CHECK(IsWindowVisible(rs1) != 0);
  CHECK(IsWindowVisible(rs2) != 0);
  CHECK(WindowProcessId(rs1) == load_only_pid);
  CHECK(WindowProcessId(rs2) == load_only_pid);
  CHECK(ControlCaption(rs1) == L"Поверхность 1");
  CHECK(ControlCaption(rs2) == L"Поверхность 2");
  CHECK(RectNear(saved_r1, WindowRect(rs1), 8));
  CHECK(RectNear(saved_r2, WindowRect(rs2), 8));
  CHECK(WaitSurfaceChromeReady(rs1, std::chrono::seconds{30}));
  CHECK(WaitSurfaceChromeReady(rs2, std::chrono::seconds{30}));
  ExpectRuChrome(rs1);
  ExpectRuChrome(rs2);
  SaveAcceptanceScreenshot(rs1, "load_only_restored_surface1");
  SaveAcceptanceScreenshot(rs2, "load_only_restored_surface2");
  CHECK(TopSurfacesWindow(load_only_pid) == rs2);

  SendMessageW(FindLanguageButton(rs1), BM_CLICK, 0, 0);
  PumpGui(std::chrono::milliseconds{500});
  CHECK(ControlCaption(rs1) == L"Surface 1");
  CHECK(ControlCaption(rs2) == L"Surface 2");
  ExpectEnChrome(rs1);
  ExpectEnChrome(rs2);

  SendMessageW(FindLanguageButton(rs1), BM_CLICK, 0, 0);
  PumpGui(std::chrono::milliseconds{500});
  CHECK(ControlCaption(rs1) == L"Поверхность 1");
  CHECK(ControlCaption(rs2) == L"Поверхность 2");
  ExpectRuChrome(rs1);
  ExpectRuChrome(rs2);

  SendMessageW(FindAddButton(rs1), BM_CLICK, 0, 0);
  HWND rs3 = nullptr;
  CHECK(WaitOwnedChild(load_only_pid, pi.hProcess, kSurfacesWindowClass,
                       L"Поверхность 3", &rs3, std::chrono::seconds{30}));
  CHECK(WindowProcessId(rs3) == load_only_pid);
  CHECK(WaitSurfaceChromeReady(rs3, std::chrono::seconds{30}));
  ExpectRuChrome(rs3);

  PostMessageW(rs1, WM_CLOSE, 0, 0);
  CHECK(WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0);
  DWORD exit_code = 1;
  CHECK(GetExitCodeProcess(pi.hProcess, &exit_code) != 0);
  CHECK(exit_code == 0);
  CloseHandle(pi.hProcess);

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->locale->language_index() == 1);

  std::filesystem::remove_all(dir);
#endif
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestPresenterHierarchy();
  apptraverse::test::TestDynamicAddLibraryPushButtonNotifierAndEnabled();
  apptraverse::test::TestLibraryLanguageSingleModelToggle();
  apptraverse::test::TestLibraryLanguageCustomLabelFreshProfile();
  apptraverse::test::TestLanguageLayoutOrientationPreservation();
  apptraverse::test::TestDynamicLanguageLibraryPushButtonNotifierAndEnabled();
  apptraverse::test::TestDynamicCloseLibraryPushButtonNotifierAndEnabled();
  apptraverse::test::TestPerWindowCloseChildCommandHandlerRouting();
  apptraverse::test::TestLibraryAddCustomLabelFreshProfile();
  apptraverse::test::TestLibraryAddCustomLabelModelEvent();
  apptraverse::test::TestLocalizationEnRuEn();
  apptraverse::test::TestLoadOnlyRestoresRuProfile();
  apptraverse::test::TestCloseButtonRemovesOne();
  apptraverse::test::TestNativeXKeepsAllSurfaces();
  apptraverse::test::TestActiveZOrderRestored();
  apptraverse::test::TestFullDesktopZOrderDistinctStacksSameTop();
  apptraverse::test::TestDesktopZOrderAfterRemoveMiddleAndAdd();
  apptraverse::test::TestControlsFollowPresentationSize();
  apptraverse::test::TestPostReleaseOuterFrameStable();
  std::cout << "surfaces_win32_smoke_test OK\n";
  return 0;
}
