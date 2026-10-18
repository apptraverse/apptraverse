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
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

#include "surface_button_presenter.h"
#include "surface_language_button_presenter.h"
#include "surface_window_presenter.h"
#include "surfaces_ids.h"
#include "surfaces_lifecycle.h"
#include "surfaces_model.h"
#include "surfaces_win32_capture.h"
#include "win_app.h"
#include "win_presenters.h"

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
  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureWin32SurfacePresenterRegistration();
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
}

void TestCloseButtonRemovesOne() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_close_btn";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureWin32SurfacePresenterRegistration();

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  HWND add1 = FindAddButton(s1);
  HWND close1 = FindCloseButton(s1);
  CHECK(add1 != nullptr);
  CHECK(close1 != nullptr);
  auto* add_owner =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(add1, GWLP_USERDATA));
  auto* close_owner =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(close1, GWLP_USERDATA));
  CHECK(add_owner != close_owner);
  CHECK(add_owner->GetClassId() == Win32SurfaceButtonPresenter::kClassId);
  CHECK(close_owner->GetClassId() == Win32SurfaceButtonPresenter::kClassId);
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

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureWin32SurfacePresenterRegistration();

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

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureWin32SurfacePresenterRegistration();

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

  // Activate Surface 2 so mobile_current / z-order restore targets it.
  // Prefer SetWindowPos + WM_ACTIVATE: SetForegroundWindow often fails when
  // the process does not own the foreground (automated smoke / agent host).
  CHECK(SetWindowPos(s2, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE) != 0);
  SendMessageW(s2, WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, FALSE), 0);
  PumpGui(std::chrono::milliseconds{200});
  CHECK(TopSurfacesWindow(pid) == s2);

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
  PumpGui(std::chrono::milliseconds{200});
  CHECK(TopSurfacesWindow(pid) == rs2);

  PostMessageW(rs2, WM_CLOSE, 0, 0);
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

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureWin32SurfacePresenterRegistration();

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

void TestLocalizationEnRuEn() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_locale";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureWin32SurfacePresenterRegistration();

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

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureWin32SurfacePresenterRegistration();

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
  CHECK(WaitOwned(load_only_pid, kSurfacesWindowClass, nullptr, &rs1,
                  std::chrono::seconds{30}));
  CHECK(WaitOwned(load_only_pid, kSurfacesWindowClass, L"Поверхность 2", &rs2,
                  std::chrono::seconds{30}));
  auto const ru_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{45};
  while (std::chrono::steady_clock::now() < ru_deadline) {
    if (WindowText(rs1) == L"Поверхность 1") {
      break;
    }
    PumpGui(std::chrono::milliseconds{50});
  }
  CHECK(WindowText(rs1) == L"Поверхность 1");
  SaveAcceptanceScreenshot(rs1, "load_only_restored_surface1");
  SaveAcceptanceScreenshot(rs2, "load_only_restored_surface2");
  CHECK(TopSurfacesWindow(load_only_pid) == rs2);

  SendMessageW(FindLanguageButton(rs1), BM_CLICK, 0, 0);
  PumpGui(std::chrono::milliseconds{500});
  CHECK(WindowText(rs1) == L"Surface 1");
  SendMessageW(FindAddButton(rs1), BM_CLICK, 0, 0);
  CHECK(WaitOwned(load_only_pid, kSurfacesWindowClass, L"Surface 3", nullptr,
                  std::chrono::seconds{30}));

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
  CHECK(application->locale->language_index() == 0);

  std::filesystem::remove_all(dir);
#endif
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestPresenterHierarchy();
  apptraverse::test::TestLocalizationEnRuEn();
  apptraverse::test::TestLoadOnlyRestoresRuProfile();
  apptraverse::test::TestCloseButtonRemovesOne();
  apptraverse::test::TestNativeXKeepsAllSurfaces();
  apptraverse::test::TestActiveZOrderRestored();
  apptraverse::test::TestControlsFollowPresentationSize();
  std::cout << "surfaces_win32_smoke_test OK\n";
  return 0;
}
