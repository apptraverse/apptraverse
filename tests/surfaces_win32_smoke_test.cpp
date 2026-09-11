#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

#include "surfaces_ids.h"
#include "surfaces_lifecycle.h"
#include "surfaces_model.h"
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

HWND FindAddButton(HWND surface) {
  return FindWindowExW(surface, nullptr, L"BUTTON", L"Add");
}

HWND FindCloseButton(HWND surface) {
  return FindWindowExW(surface, nullptr, L"BUTTON", L"Close this window");
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
                                    Win32SurfacePresenter::kClassId) == 1);
  CHECK(registry.GenerationDistance(SurfacePresenter::kClassId,
                                    Win32SurfacePresenter::kClassId) == 2);
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
  CHECK(add_owner == close_owner);
  CHECK(add_owner->GetClassId() == Win32SurfacePresenter::kClassId);
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
  if (client_w > client_h) {
    CHECK(width > height);
  } else {
    CHECK(width <= height);
  }
}

RECT ScreenRect(HWND hwnd) {
  RECT rect{};
  CHECK(GetWindowRect(hwnd, &rect) != 0);
  return rect;
}

void ExpectHorizontalButtons(HWND surface) {
  RECT const add = ScreenRect(FindAddButton(surface));
  RECT const close = ScreenRect(FindCloseButton(surface));
  CHECK(add.left < close.left);
  CHECK(std::abs(add.top - close.top) <= 2);
}

void ExpectVerticalButtons(HWND surface) {
  RECT const add = ScreenRect(FindAddButton(surface));
  RECT const close = ScreenRect(FindCloseButton(surface));
  CHECK(add.top < close.top);
  CHECK(std::abs(add.left - close.left) <= 2);
}

void TestControlsFollowClientAspectRatio() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_aspect_layout";
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
  ExpectHorizontalButtons(s1);

  PlaceClientSize(s1, 400, 800);
  ExpectVerticalButtons(s1);

  PlaceClientSize(s1, 500, 500);
  ExpectVerticalButtons(s1);

  // Live switch back to landscape without recreating the window.
  PlaceClientSize(s1, 700, 300);
  ExpectHorizontalButtons(s1);

  // Add still works after layout switches.
  SendMessageW(FindAddButton(s1), BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  PlaceClientSize(s2, 360, 640);
  ExpectVerticalButtons(s2);
  SendMessageW(FindCloseButton(s2), BM_CLICK, 0, 0);
  CHECK(WaitCount(pid, kSurfacesWindowClass, 1, std::chrono::seconds{30}));
  CHECK(IsWindow(s1) != 0);

  PostMessageW(s1, WM_CLOSE, 0, 0);
  gui.join();
  std::filesystem::remove_all(dir);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestPresenterHierarchy();
  apptraverse::test::TestCloseButtonRemovesOne();
  apptraverse::test::TestNativeXKeepsAllSurfaces();
  apptraverse::test::TestActiveZOrderRestored();
  apptraverse::test::TestControlsFollowClientAspectRatio();
  std::cout << "surfaces_win32_smoke_test OK\n";
  return 0;
}
