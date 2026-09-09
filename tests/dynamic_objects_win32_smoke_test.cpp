#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cwchar>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

#include "dynamic_ids.h"
#include "dynamic_lifecycle.h"
#include "dynamic_model.h"
#include "dynamic_win32_messages.h"
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

bool ChildHasText(HWND parent, wchar_t const* wanted) {
  struct Ctx {
    wchar_t const* wanted;
    bool found;
  } ctx{wanted, false};
  EnumChildWindows(
      parent,
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        wchar_t text[256]{};
        if (GetWindowTextW(hwnd, text, 256) > 0 &&
            wcscmp(text, c->wanted) == 0) {
          c->found = true;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.found;
}

HWND FindRemoveButtonForItem(HWND main, wchar_t const* item_text) {
  HWND item_static = nullptr;
  struct FindStatic {
    wchar_t const* item_text;
    HWND* out;
  } find_static{item_text, &item_static};
  EnumChildWindows(
      main,
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<FindStatic*>(lparam);
        wchar_t text[256]{};
        if (GetWindowTextW(hwnd, text, 256) > 0 &&
            wcscmp(text, c->item_text) == 0) {
          *c->out = hwnd;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&find_static));
  if (item_static == nullptr) {
    return nullptr;
  }
  HWND parent = GetParent(item_static);
  RECT item_rect{};
  GetWindowRect(item_static, &item_rect);
  HWND button = nullptr;
  struct FindButton {
    LONG top;
    HWND* out;
  } find_button{item_rect.top, &button};
  EnumChildWindows(
      parent,
      [](HWND child, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<FindButton*>(lparam);
        wchar_t text[32]{};
        if (GetWindowTextW(child, text, 32) <= 0 || wcscmp(text, L"x") != 0) {
          return TRUE;
        }
        RECT button_rect{};
        GetWindowRect(child, &button_rect);
        if (button_rect.top == c->top) {
          *c->out = child;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&find_button));
  return button;
}

int CountChildText(HWND parent, wchar_t const* wanted) {
  struct Ctx {
    wchar_t const* wanted;
    int count;
  } ctx{wanted, 0};
  EnumChildWindows(
      parent,
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        wchar_t text[256]{};
        if (GetWindowTextW(hwnd, text, 256) > 0 &&
            wcscmp(text, c->wanted) == 0) {
          ++c->count;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.count;
}

void PumpGui(std::chrono::milliseconds slice) {
  MsgWaitForMultipleObjects(0, nullptr, FALSE,
                             static_cast<DWORD>(slice.count()),
                             QS_ALLINPUT | QS_ALLPOSTMESSAGE);
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
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

bool WaitChildText(HWND parent, wchar_t const* text,
                   std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (ChildHasText(parent, text)) {
      return true;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  return false;
}

void WaitPublished(DynamicModelSession& session) {
  std::unique_lock<std::mutex> lock{session.mu};
  CHECK(session.cv.wait_for(lock, std::chrono::seconds{30}, [&] {
    return session.channel.has_unread_published();
  }));
}

void TestInProcessAddCreatesRow() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_dynamic_objects_inproc";
  std::filesystem::remove_all(dir);

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND main = nullptr;
  CHECK(WaitOwned(pid, kDynamicMainClass, kDynamicMainTitle, &main,
                  std::chrono::seconds{30}));
  CHECK(CountOwnedClass(pid, kDynamicMainClass) == 1);
  CHECK(WaitChildText(main, L"Item 1", std::chrono::seconds{10}));
  CHECK(CountChildText(main, L"Item 1") == 1);
  CHECK(!ChildHasText(main, L"Item 2"));

  HWND add = FindWindowExW(main, nullptr, L"BUTTON", L"Add item");
  CHECK(add != nullptr);
  auto* add_owner =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(add, GWLP_USERDATA));
  CHECK(add_owner != nullptr);
  CHECK(add_owner->GetClassId() == Win32AddItemPresenter::kClassId);
  CHECK(static_cast<Win32AddItemPresenter*>(add_owner)->hwnd == add);
  SendMessageW(add, BM_CLICK, 0, 0);

  CHECK(WaitChildText(main, L"Item 2", std::chrono::seconds{30}));
  CHECK(CountChildText(main, L"Item 1") == 1);
  CHECK(CountChildText(main, L"Item 2") == 1);
  CHECK(CountOwnedClass(pid, kDynamicMainClass) == 1);

  SendMessageW(add, BM_CLICK, 0, 0);
  CHECK(WaitChildText(main, L"Item 3", std::chrono::seconds{30}));

  HWND remove2 = FindRemoveButtonForItem(main, L"Item 2");
  CHECK(remove2 != nullptr);
  SendMessageW(remove2, BM_CLICK, 0, 0);
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while (std::chrono::steady_clock::now() < deadline) {
    if (!ChildHasText(main, L"Item 2") && ChildHasText(main, L"Item 1") &&
        ChildHasText(main, L"Item 3")) {
      break;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  CHECK(!ChildHasText(main, L"Item 2"));
  CHECK(ChildHasText(main, L"Item 1"));
  CHECK(ChildHasText(main, L"Item 3"));

  SendMessageW(add, BM_CLICK, 0, 0);
  CHECK(WaitChildText(main, L"Item 4", std::chrono::seconds{30}));
  CHECK(!ChildHasText(main, L"Item 2"));

  HWND remove1 = FindRemoveButtonForItem(main, L"Item 1");
  CHECK(remove1 != nullptr);
  SendMessageW(remove1, BM_CLICK, 0, 0);
  auto const deadline2 =
      std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while (std::chrono::steady_clock::now() < deadline2) {
    if (!ChildHasText(main, L"Item 1") && ChildHasText(main, L"Item 3") &&
        ChildHasText(main, L"Item 4")) {
      break;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  CHECK(!ChildHasText(main, L"Item 1"));
  CHECK(ChildHasText(main, L"Item 3"));
  CHECK(ChildHasText(main, L"Item 4"));
  CHECK(CountOwnedClass(pid, kDynamicMainClass) == 1);

  SendMessageW(main, WM_CLOSE, 0, 0);
  CHECK(WaitForSingleObject(gui.native_handle(), 30000) == WAIT_OBJECT_0);
  gui.join();

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{dynamic_objects::ToObjId(
                  dynamic_objects::ObjId::Application)});
  CHECK(application->main_window->item_list->items.size() == 2);
  CHECK(application->main_window->item_list->items[0]->number == 3);
  CHECK(application->main_window->item_list->items[1]->number == 4);

  std::filesystem::remove_all(dir);
}

void TestModelSessionDoesNotCreateMain() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_dynamic_objects_model_no_hwnd";
  std::filesystem::remove_all(dir);
  CHECK(CountOwnedClass(pid, kDynamicMainClass) == 0);

  DynamicModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);
  CHECK(CountOwnedClass(pid, kDynamicMainClass) == 0);
  session.RequestStop();
  model.join();
  CHECK(CountOwnedClass(pid, kDynamicMainClass) == 0);
  std::filesystem::remove_all(dir);
}

#ifdef WIN32_DYNAMIC_OBJECTS_DEMO_EXE

struct ChildProcess {
  HANDLE process;
  DWORD pid;
};

ChildProcess StartDemo(std::filesystem::path const& exe,
                       std::filesystem::path const& state_dir) {
  std::wstring cmd = L"\"" + exe.wstring() + L"\" --state-dir \"" +
                     state_dir.wstring() + L"\"";
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(exe.wstring().c_str(), buf.data(), nullptr, nullptr,
                            FALSE, 0, nullptr, nullptr, &si, &pi);
  CHECK(ok);
  CloseHandle(pi.hThread);
  return ChildProcess{pi.hProcess, pi.dwProcessId};
}

void CloseAndWait(ChildProcess& child, HWND main) {
  SendMessageW(main, WM_CLOSE, 0, 0);
  CHECK(WaitForSingleObject(child.process, 60000) == WAIT_OBJECT_0);
  DWORD code = 1;
  GetExitCodeProcess(child.process, &code);
  CHECK(code == 0);
  CloseHandle(child.process);
}

void TestChildProcessAddThenLoadOnly() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_dynamic_objects_child";
  std::filesystem::remove_all(dir);

  {
    auto child = StartDemo(std::filesystem::path{WIN32_DYNAMIC_OBJECTS_DEMO_EXE},
                           dir);
    HWND main = nullptr;
    CHECK(WaitOwned(child.pid, kDynamicMainClass, kDynamicMainTitle, &main,
                    std::chrono::seconds{60}));
    CHECK(CountOwnedClass(child.pid, kDynamicMainClass) == 1);
    CHECK(WaitChildText(main, L"Item 1", std::chrono::seconds{30}));

    HWND add = FindWindowExW(main, nullptr, L"BUTTON", L"Add item");
    CHECK(add != nullptr);
    SendMessageW(add, BM_CLICK, 0, 0);
    CHECK(WaitChildText(main, L"Item 2", std::chrono::seconds{30}));
    SendMessageW(add, BM_CLICK, 0, 0);
    CHECK(WaitChildText(main, L"Item 3", std::chrono::seconds{30}));
    HWND remove2 = FindRemoveButtonForItem(main, L"Item 2");
    CHECK(remove2 != nullptr);
    SendMessageW(remove2, BM_CLICK, 0, 0);
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (std::chrono::steady_clock::now() < deadline) {
      if (!ChildHasText(main, L"Item 2") && ChildHasText(main, L"Item 3")) {
        break;
      }
      PumpGui(std::chrono::milliseconds{20});
    }
    CHECK(!ChildHasText(main, L"Item 2"));
    SendMessageW(add, BM_CLICK, 0, 0);
    CHECK(WaitChildText(main, L"Item 4", std::chrono::seconds{30}));
    HWND remove1 = FindRemoveButtonForItem(main, L"Item 1");
    CHECK(remove1 != nullptr);
    SendMessageW(remove1, BM_CLICK, 0, 0);
    auto const deadline2 =
        std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (std::chrono::steady_clock::now() < deadline2) {
      if (!ChildHasText(main, L"Item 1") && ChildHasText(main, L"Item 3") &&
          ChildHasText(main, L"Item 4")) {
        break;
      }
      PumpGui(std::chrono::milliseconds{20});
    }
    CHECK(CountOwnedClass(child.pid, kDynamicMainClass) == 1);
    CloseAndWait(child, main);
  }

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{dynamic_objects::ToObjId(
                  dynamic_objects::ObjId::Application)});
  CHECK(application->main_window->item_list->items.size() == 2);
  CHECK(application->main_window->item_list->items[0]->number == 3);
  CHECK(application->main_window->item_list->items[1]->number == 4);

  {
    auto child =
        StartDemo(std::filesystem::path{WIN32_DYNAMIC_OBJECTS_LOAD_ONLY_EXE},
                  dir);
    HWND main = nullptr;
    CHECK(WaitOwned(child.pid, kDynamicMainClass, kDynamicMainTitle, &main,
                    std::chrono::seconds{60}));
    CHECK(WaitChildText(main, L"Item 3", std::chrono::seconds{30}));
    CHECK(WaitChildText(main, L"Item 4", std::chrono::seconds{30}));
    CHECK(!ChildHasText(main, L"Item 1"));
    CHECK(!ChildHasText(main, L"Item 2"));
    CHECK(CountOwnedClass(child.pid, kDynamicMainClass) == 1);
    CloseAndWait(child, main);
  }

  std::filesystem::remove_all(dir);
}

#endif

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestModelSessionDoesNotCreateMain();
  apptraverse::test::TestInProcessAddCreatesRow();
#ifdef WIN32_DYNAMIC_OBJECTS_DEMO_EXE
  apptraverse::test::TestChildProcessAddThenLoadOnly();
#endif
  std::cout << "dynamic_objects_win32_smoke_test OK\n";
  return 0;
}
