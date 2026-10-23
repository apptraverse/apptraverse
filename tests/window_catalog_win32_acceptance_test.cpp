#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#undef RegisterClass
#endif

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"

#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

constexpr std::uint32_t kListControlId = 5001;
constexpr std::uint32_t kAddControlId = 5002;
constexpr std::uint32_t kOpenControlId = 5003;
constexpr std::uint32_t kDeleteControlId = 5004;
constexpr std::uint32_t kLanguageControlId = 5005;

std::filesystem::path ExeDir() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return std::filesystem::path{path}.parent_path();
}

std::filesystem::path DemoExe(char const* name) {
  auto const dir = ExeDir();
  auto candidate = dir / name;
  if (std::filesystem::exists(candidate)) {
    return candidate;
  }
  return dir / ".." / "examples" / "surfaces_demo" / "windows" / name;
}

std::filesystem::path FreshStateDir(char const* tag) {
  auto dir = std::filesystem::temp_directory_path() / "apptraverse_test" /
             tag;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

int CountChildClass(HWND parent, wchar_t const* class_name);

bool WaitUntil(std::chrono::milliseconds timeout, auto predicate) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    Sleep(25);
  }
  return predicate();
}

HWND WaitCatalogMainWindow(DWORD pid, std::chrono::milliseconds timeout) {
  HWND found = nullptr;
  WaitUntil(timeout, [&] {
    struct Ctx {
      DWORD pid;
      HWND* out;
    } ctx{pid, &found};
    EnumWindows(
        [](HWND hwnd, LPARAM lparam) -> BOOL {
          auto* c = reinterpret_cast<Ctx*>(lparam);
          DWORD window_pid = 0;
          GetWindowThreadProcessId(hwnd, &window_pid);
          if (window_pid != c->pid || !IsWindowVisible(hwnd)) {
            return TRUE;
          }
          wchar_t cls[64]{};
          GetClassNameW(hwnd, cls, 64);
          if (wcscmp(cls, L"AppTraverseWindowCatalogLoading") == 0) {
            return TRUE;
          }
          if (CountChildClass(hwnd, L"ListBox") < 1) {
            return TRUE;
          }
          *c->out = hwnd;
          return FALSE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return found != nullptr;
  });
  return found;
}

struct CountChildClassCtx {
  wchar_t const* class_name;
  int count;
};

int CountChildClass(HWND parent, wchar_t const* class_name) {
  CountChildClassCtx ctx{class_name, 0};
  EnumChildWindows(
      parent,
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<CountChildClassCtx*>(lparam);
        wchar_t cls[64]{};
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, c->class_name) == 0) {
          ++c->count;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.count;
}

std::vector<HWND> TopLevelWindowsForPid(DWORD pid) {
  std::vector<HWND> out;
  struct Ctx {
    DWORD pid;
    std::vector<HWND>* out_hwnds;
  } ctx{pid, &out};
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        DWORD p = 0;
        GetWindowThreadProcessId(hwnd, &p);
        if (p != c->pid || !IsWindowVisible(hwnd)) {
          return TRUE;
        }
        wchar_t cls[64]{};
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, L"AppTraverseWindowCatalogLoading") == 0) {
          return TRUE;
        }
        c->out_hwnds->push_back(hwnd);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return out;
}

void ClickControl(HWND parent, std::uint32_t control_id) {
  HWND const child = GetDlgItem(parent, static_cast<int>(control_id));
  CHECK(child != nullptr);
  SendMessageW(parent, WM_COMMAND,
               MAKEWPARAM(control_id, BN_CLICKED),
               reinterpret_cast<LPARAM>(child));
}

std::wstring GetWindowTextHwnd(HWND hwnd) {
  int const len = GetWindowTextLengthW(hwnd);
  std::wstring text(static_cast<std::size_t>(len + 1), L'\0');
  if (len > 0) {
    GetWindowTextW(hwnd, text.data(), len + 1);
  }
  text.resize(static_cast<std::size_t>(len));
  return text;
}

std::wstring GetControlText(HWND parent, std::uint32_t id) {
  return GetWindowTextHwnd(GetDlgItem(parent, static_cast<int>(id)));
}

std::wstring ListBoxItem(HWND list, int index) {
  LRESULT const len = SendMessageW(list, LB_GETTEXTLEN, index, 0);
  if (len == LB_ERR) {
    return {};
  }
  std::wstring buf(static_cast<std::size_t>(len + 1), L'\0');
  SendMessageW(list, LB_GETTEXT, index,
               reinterpret_cast<LPARAM>(buf.data()));
  buf.resize(static_cast<std::size_t>(len));
  return buf;
}

HWND FindTopLevelWithTitle(DWORD pid, std::wstring const& needle) {
  for (HWND h : TopLevelWindowsForPid(pid)) {
    if (GetWindowTextHwnd(h).find(needle) != std::wstring::npos) {
      return h;
    }
  }
  return nullptr;
}

void WaitChromeReady(HWND main) {
  CHECK(WaitUntil(std::chrono::seconds{20}, [&] {
    return CountChildClass(main, L"Button") >= 4;
  }));
}

void EnsureCatalogMainWindow(DWORD pid, HWND& main) {
  if (main != nullptr && IsWindow(main) &&
      CountChildClass(main, L"ListBox") >= 1) {
    return;
  }
  main = WaitCatalogMainWindow(pid, std::chrono::seconds{10});
  CHECK(main != nullptr);
  WaitChromeReady(main);
}

void SelectListIndex(HWND main, int index) {
  HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
  CHECK(list != nullptr);
  SendMessageW(list, LB_SETCURSEL, index, 0);
  SendMessageW(main, WM_COMMAND,
               MAKEWPARAM(kListControlId, LBN_SELCHANGE),
               reinterpret_cast<LPARAM>(list));
}

bool ListRowsMatch(HWND main, std::vector<std::wstring> const& expected) {
  HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
  if (!list) {
    return false;
  }
  LRESULT const count = SendMessageW(list, LB_GETCOUNT, 0, 0);
  if (static_cast<std::size_t>(count) != expected.size()) {
    return false;
  }
  for (std::size_t i = 0; i < expected.size(); ++i) {
    if (ListBoxItem(list, static_cast<int>(i)) != expected[i]) {
      return false;
    }
  }
  return true;
}

void ExpectListRows(HWND main, std::vector<std::wstring> const& expected) {
  CHECK(WaitUntil(std::chrono::seconds{15}, [&] {
    return ListRowsMatch(main, expected);
  }));
}

void ExpectChromeEn(HWND main) {
  CHECK(GetControlText(main, kAddControlId) == L"Add");
  CHECK(GetControlText(main, kOpenControlId) == L"Open");
  CHECK(GetControlText(main, kDeleteControlId) == L"Delete");
  CHECK(GetControlText(main, kLanguageControlId) == L"RU");
}

void ExpectChromeRu(HWND main) {
  CHECK(GetControlText(main, kAddControlId) == L"\x0414\x043E\x0431\x0430\x0432\x0438\x0442\x044C");
  CHECK(GetControlText(main, kOpenControlId) == L"\x041E\x0442\x043A\x0440\x044B\x0442\x044C");
  CHECK(GetControlText(main, kDeleteControlId) == L"\x0423\x0434\x0430\x043B\x0438\x0442\x044C");
  CHECK(GetControlText(main, kLanguageControlId) == L"EN");
}

PROCESS_INFORMATION StartDemo(std::filesystem::path const& exe,
                              std::filesystem::path const& state_dir) {
  std::wstring cmd = L"\"" + exe.wstring() + L"\" --state-dir \"" +
                     state_dir.wstring() + L"\"";
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  CHECK(CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &si, &pi));
  CloseHandle(pi.hThread);
  return pi;
}

void CloseProcessGracefully(HWND main, PROCESS_INFORMATION& pi) {
  SendMessageW(main, WM_CLOSE, 0, 0);
  CHECK(WaitUntil(std::chrono::seconds{20}, [&] {
    return WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0;
  }));
  DWORD exit_code = 1;
  CHECK(GetExitCodeProcess(pi.hProcess, &exit_code));
  CHECK(exit_code == 0);
  CloseHandle(pi.hProcess);
}

std::string ProbeRowPrimaryUtf8(WindowHolder const& holder, int language_index) {
  CHECK(holder.primary_text.is_valid());
  if (!holder.primary_text.is_loaded()) {
    holder.primary_text.Load();
  }
  ui::UiLocalizationString const& localized = *holder.primary_text;
  if (language_index >= 0 &&
      static_cast<std::size_t>(language_index) < localized.strings.size()) {
    ui::UiString::ptr const& entry = localized.strings[language_index];
    if (entry.is_valid() && !entry.is_loaded()) {
      entry.Load();
    }
  }
  return localized.Resolve(language_index);
}

void ProbeProfileListRows(
    std::filesystem::path const& state_dir, std::size_t expected_rows,
    std::vector<ae::ObjId>* out_holder_ids = nullptr,
    std::vector<ae::ObjId> const* expected_holder_prefix = nullptr) {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  auto catalog = LoadApplication<WindowCatalog>(
      domain,
      ae::ObjId{window_catalog_demo::ToObjId(
          window_catalog_demo::ObjId::Catalog)});
  CHECK(catalog->list.is_valid());
  CHECK(catalog->list->rows.size() == expected_rows);
  if (expected_holder_prefix != nullptr) {
    CHECK(expected_holder_prefix->size() <= expected_rows);
    for (std::size_t i = 0; i < expected_holder_prefix->size(); ++i) {
      auto* h =
          static_cast<WindowHolder*>(catalog->list->rows[i].operator->());
      CHECK(h->obj_id == (*expected_holder_prefix)[i]);
    }
  }
  if (out_holder_ids != nullptr) {
    out_holder_ids->clear();
    out_holder_ids->reserve(expected_rows);
  }
  for (std::size_t i = 0; i < expected_rows; ++i) {
    auto* h = static_cast<WindowHolder*>(catalog->list->rows[i].operator->());
    CHECK(h->window_number == static_cast<std::uint32_t>(i + 1));
    std::string const title = ProbeRowPrimaryUtf8(*h, 0);
    CHECK(title == ("Window " + std::to_string(h->window_number)));
    if (out_holder_ids != nullptr) {
      out_holder_ids->push_back(h->obj_id);
    }
    if (h->window.is_valid() && h->window.is_loaded()) {
      CHECK(domain.Find(h->window.id()) != nullptr);
    } else if (h->window.is_valid()) {
      CHECK(domain.Find(h->window.id()) == nullptr);
    }
  }
  CHECK(catalog->next_window_number ==
        static_cast<std::uint32_t>(expected_rows + 1));
}

void RunRestartPersistenceAcceptance() {
  auto const state_dir = FreshStateDir("window_catalog_win32_acceptance");
  auto const demo = DemoExe("win32_window_catalog_demo.exe");
  auto const load_only = DemoExe("win32_window_catalog_demo_load_only.exe");
  CHECK(std::filesystem::exists(demo));
  CHECK(std::filesystem::exists(load_only));

  PROCESS_INFORMATION pi = StartDemo(demo, state_dir);
  HWND main = WaitCatalogMainWindow(pi.dwProcessId, std::chrono::seconds{30});
  CHECK(main != nullptr);
  WaitChromeReady(main);

  for (int i = 0; i < 3; ++i) {
    ClickControl(main, kAddControlId);
    WaitUntil(std::chrono::seconds{5}, [&] {
      HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
      return list && SendMessageW(list, LB_GETCOUNT, 0, 0) > i;
    });
  }
  ExpectListRows(main, {L"Window 1", L"Window 2", L"Window 3"});

  HWND const child2 =
      FindTopLevelWithTitle(pi.dwProcessId, L"Window 2");
  CHECK(child2 != nullptr);
  SendMessageW(child2, WM_CLOSE, 0, 0);
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return FindTopLevelWithTitle(pi.dwProcessId, L"Window 2") == nullptr;
  }));
  ExpectListRows(main, {L"Window 1", L"Window 2", L"Window 3"});

  CloseProcessGracefully(main, pi);
  std::vector<ae::ObjId> saved_holder_ids;
  ProbeProfileListRows(state_dir, 3, &saved_holder_ids);
  CHECK(saved_holder_ids.size() == 3);

  pi = StartDemo(load_only, state_dir);
  main = WaitCatalogMainWindow(pi.dwProcessId, std::chrono::seconds{30});
  CHECK(main != nullptr);
  WaitChromeReady(main);
  ExpectListRows(main, {L"Window 1", L"Window 2", L"Window 3"});
  CHECK(FindTopLevelWithTitle(pi.dwProcessId, L"Window 1") != nullptr);
  CHECK(FindTopLevelWithTitle(pi.dwProcessId, L"Window 2") == nullptr);
  CHECK(FindTopLevelWithTitle(pi.dwProcessId, L"Window 3") != nullptr);
  CHECK(TopLevelWindowsForPid(pi.dwProcessId).size() == 3);

  HWND const list_hwnd = GetDlgItem(main, static_cast<int>(kListControlId));
  CHECK(list_hwnd != nullptr);
  SendMessageW(list_hwnd, LB_SETCURSEL, 1, 0);
  SendMessageW(main, WM_COMMAND,
               MAKEWPARAM(kListControlId, LBN_DBLCLK),
               reinterpret_cast<LPARAM>(list_hwnd));
  CHECK(WaitUntil(std::chrono::seconds{15}, [&] {
    return FindTopLevelWithTitle(pi.dwProcessId, L"Window 2") != nullptr;
  }));

  ClickControl(main, kAddControlId);
  ExpectListRows(main, {L"Window 1", L"Window 2", L"Window 3", L"Window 4"});

  CloseProcessGracefully(main, pi);
  ProbeProfileListRows(state_dir, 4, nullptr, &saved_holder_ids);
  CHECK(saved_holder_ids.size() == 3);

  pi = StartDemo(demo, state_dir);
  main = WaitCatalogMainWindow(pi.dwProcessId, std::chrono::seconds{30});
  CHECK(main != nullptr);
  WaitChromeReady(main);
  ExpectListRows(main, {L"Window 1", L"Window 2", L"Window 3", L"Window 4"});
  CHECK(WaitUntil(std::chrono::seconds{30}, [&] {
    return TopLevelWindowsForPid(pi.dwProcessId).size() >= 4;
  }));
  CHECK(FindTopLevelWithTitle(pi.dwProcessId, L"Window 1") != nullptr);
  CHECK(FindTopLevelWithTitle(pi.dwProcessId, L"Window 2") != nullptr);
  CHECK(FindTopLevelWithTitle(pi.dwProcessId, L"Window 3") != nullptr);
  CHECK(FindTopLevelWithTitle(pi.dwProcessId, L"Window 4") != nullptr);

  CloseProcessGracefully(main, pi);
}

void RunLanguageAndFocusAcceptance() {
  auto const state_dir = FreshStateDir("window_catalog_win32_lang_focus");
  auto const demo = DemoExe("win32_window_catalog_demo.exe");
  CHECK(std::filesystem::exists(demo));

  PROCESS_INFORMATION pi = StartDemo(demo, state_dir);
  HWND main = WaitCatalogMainWindow(pi.dwProcessId, std::chrono::seconds{30});
  CHECK(main != nullptr);
  WaitChromeReady(main);
  ExpectChromeEn(main);
  CHECK(GetWindowTextHwnd(main).find(L"Window Catalog") != std::wstring::npos);

  for (int i = 0; i < 3; ++i) {
    ClickControl(main, kAddControlId);
    WaitUntil(std::chrono::seconds{5}, [&] {
      HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
      return list && SendMessageW(list, LB_GETCOUNT, 0, 0) > i;
    });
  }
  ExpectListRows(main, {L"Window 1", L"Window 2", L"Window 3"});
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return TopLevelWindowsForPid(pi.dwProcessId).size() >= 4;
  }));

  HWND const child2_before =
      FindTopLevelWithTitle(pi.dwProcessId, L"Window 2");
  CHECK(child2_before != nullptr);
  SendMessageW(child2_before, WM_CLOSE, 0, 0);
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return !IsWindow(child2_before);
  }));
  SelectListIndex(main, 1);
  ClickControl(main, kOpenControlId);
  HWND child2_reopened = nullptr;
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    child2_reopened = FindTopLevelWithTitle(pi.dwProcessId, L"Window 2");
    return child2_reopened != nullptr;
  }));
  CHECK(child2_reopened != child2_before);

  ClickControl(main, kOpenControlId);
  Sleep(100);
  CHECK(FindTopLevelWithTitle(pi.dwProcessId, L"Window 2") == child2_reopened);

  ShowWindow(child2_reopened, SW_MINIMIZE);
  CHECK(WaitUntil(std::chrono::seconds{5}, [&] {
    return IsIconic(child2_reopened);
  }));
  SelectListIndex(main, 1);
  ClickControl(main, kOpenControlId);
  CHECK(WaitUntil(std::chrono::seconds{5}, [&] {
    return !IsIconic(child2_reopened);
  }));

  ClickControl(main, kLanguageControlId);
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return GetControlText(main, kAddControlId).find(L"\x0414") !=
           std::wstring::npos;
  }));
  ExpectChromeRu(main);
  ExpectListRows(main,
                 {L"\x041E\x043A\x043D\x043E 1", L"\x041E\x043A\x043D\x043E 2",
                  L"\x041E\x043A\x043D\x043E 3"});

  ClickControl(main, kLanguageControlId);
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return GetControlText(main, kAddControlId) == L"Add";
  }));
  EnsureCatalogMainWindow(pi.dwProcessId, main);
  ExpectChromeEn(main);

  CloseProcessGracefully(main, pi);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunRestartPersistenceAcceptance();
  apptraverse::test::RunLanguageAndFocusAcceptance();
  std::cout << "window_catalog_win32_acceptance_test OK\n";
  return 0;
}
