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

HWND FindAddButton(HWND surface) {
  return FindWindowExW(surface, nullptr, L"BUTTON", L"Add");
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

void TestInProcessMultiWindow() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_win32_inproc";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureWin32SurfacePresenterRegistration();

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND s1 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 1", &s1,
                  std::chrono::seconds{30}));
  CHECK(CountOwnedClass(pid, kSurfacesWindowClass) == 1);
  HWND add1 = FindAddButton(s1);
  CHECK(add1 != nullptr);
  auto* owner1 =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(add1, GWLP_USERDATA));
  CHECK(owner1 != nullptr);
  CHECK(owner1->GetClassId() == Win32SurfacePresenter::kClassId);
  CHECK(static_cast<Win32SurfacePresenter*>(
            static_cast<SurfacePresenter*>(owner1))
            ->hwnd == s1);

  SendMessageW(add1, BM_CLICK, 0, 0);
  HWND s2 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 2", &s2,
                  std::chrono::seconds{30}));
  CHECK(WaitCount(pid, kSurfacesWindowClass, 2, std::chrono::seconds{10}));
  CHECK(IsWindow(s1) != 0);
  CHECK(s1 != s2);

  HWND add2 = FindAddButton(s2);
  CHECK(add2 != nullptr);
  SendMessageW(add2, BM_CLICK, 0, 0);
  HWND s3 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 3", &s3,
                  std::chrono::seconds{30}));
  CHECK(WaitCount(pid, kSurfacesWindowClass, 3, std::chrono::seconds{10}));
  CHECK(IsWindow(s1) != 0);
  CHECK(IsWindow(s2) != 0);

  // Close middle Surface 2 via WM_CLOSE → RemoveClick → OnUnload.
  PostMessageW(s2, WM_CLOSE, 0, 0);
  CHECK(WaitCount(pid, kSurfacesWindowClass, 2, std::chrono::seconds{30}));
  CHECK(IsWindow(s1) != 0);
  CHECK(IsWindow(s3) != 0);
  CHECK(IsWindow(s2) == 0);
  CHECK(FindOwned(pid, kSurfacesWindowClass, L"Surface 1") == s1);
  CHECK(FindOwned(pid, kSurfacesWindowClass, L"Surface 3") == s3);

  HWND add3 = FindAddButton(s3);
  CHECK(add3 != nullptr);
  SendMessageW(add3, BM_CLICK, 0, 0);
  HWND s4 = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 4", &s4,
                  std::chrono::seconds{30}));
  CHECK(WaitCount(pid, kSurfacesWindowClass, 3, std::chrono::seconds{10}));
  CHECK(FindOwned(pid, kSurfacesWindowClass, L"Surface 2") == nullptr);

  // Close Surface 1 and Surface 4; leave Surface 3 as last.
  PostMessageW(s1, WM_CLOSE, 0, 0);
  CHECK(WaitCount(pid, kSurfacesWindowClass, 2, std::chrono::seconds{30}));
  PostMessageW(s4, WM_CLOSE, 0, 0);
  CHECK(WaitCount(pid, kSurfacesWindowClass, 1, std::chrono::seconds{30}));
  CHECK(FindOwned(pid, kSurfacesWindowClass, L"Surface 3") == s3);

  // Last window X → exit without removing Surface 3.
  PostMessageW(s3, WM_CLOSE, 0, 0);
  gui.join();
  CHECK(CountOwnedClass(pid, kSurfacesWindowClass) == 0);

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->surfaces.size() == 1);
  CHECK(application->surfaces->surfaces[0]->number == 3);
  auto const persisted_id = application->surfaces->surfaces[0]->obj_id;

  // Restart restores Surface 3.
  WinApp app2;
  std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
  HWND restarted = nullptr;
  CHECK(WaitOwned(pid, kSurfacesWindowClass, L"Surface 3", &restarted,
                  std::chrono::seconds{30}));
  CHECK(CountOwnedClass(pid, kSurfacesWindowClass) == 1);
  CHECK(FindOwned(pid, kSurfacesWindowClass, L"Surface 1") == nullptr);

  PostMessageW(restarted, WM_CLOSE, 0, 0);
  gui2.join();

  DirectoryDomainStorage storage2{dir};
  ae::Domain domain2{storage2};
  auto application2 = LoadApplication<Application>(
      domain2, ae::ObjId{surfaces_demo::ToObjId(
                   surfaces_demo::ObjId::Application)});
  CHECK(application2->surfaces->surfaces.size() == 1);
  CHECK(application2->surfaces->surfaces[0]->number == 3);
  CHECK(application2->surfaces->surfaces[0]->obj_id == persisted_id);

  std::filesystem::remove_all(dir);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestPresenterHierarchy();
  apptraverse::test::TestInProcessMultiWindow();
  std::cout << "surfaces_win32_smoke_test OK\n";
  return 0;
}
