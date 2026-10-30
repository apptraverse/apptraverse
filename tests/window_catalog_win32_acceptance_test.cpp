#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#undef RegisterClass
#endif

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"

#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::test {
namespace {

void TerminateProcessHandle(PROCESS_INFORMATION& pi) {
  if (pi.hProcess == nullptr) {
    return;
  }
  TerminateProcess(pi.hProcess, 1);
  WaitForSingleObject(pi.hProcess, 5000);
  CloseHandle(pi.hProcess);
  pi.hProcess = nullptr;
}

struct ActiveDemoRegistry {
  std::vector<PROCESS_INFORMATION*> processes;

  void Add(PROCESS_INFORMATION* pi) { processes.push_back(pi); }

  void Remove(PROCESS_INFORMATION* pi) {
    auto it = std::find(processes.begin(), processes.end(), pi);
    if (it != processes.end()) {
      processes.erase(it);
    }
  }

  void TerminateAll() {
    for (PROCESS_INFORMATION* pi : processes) {
      TerminateProcessHandle(*pi);
    }
    processes.clear();
  }
};

ActiveDemoRegistry& ActiveDemos() {
  static ActiveDemoRegistry registry;
  return registry;
}

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      ActiveDemos().TerminateAll();                                          \
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
  std::error_code ec;
  auto const try_path = [&](std::filesystem::path const& p) {
    auto abs = std::filesystem::weakly_canonical(p, ec);
    if (!ec && std::filesystem::exists(abs)) {
      return abs;
    }
    return std::filesystem::path{};
  };
  if (auto p = try_path(ExeDir() / name); !p.empty()) {
    return p;
  }
  if (auto p = try_path(ExeDir() / ".." / "examples" / "surfaces_demo" /
                        "windows" / name);
      !p.empty()) {
    return p;
  }
  return ExeDir() / name;
}

struct CatalogFrame {
  int x = 0;
  int y = 0;
  int client_width = 0;
  int client_height = 0;
};

CatalogFrame ReadCatalogFrame(HWND hwnd) {
  CatalogFrame frame;
  apptraverse::ui::windows::ReadModelFrameFromHwnd(
      hwnd, frame.x, frame.y, frame.client_width, frame.client_height);
  return frame;
}

bool FramesNearEqual(CatalogFrame const& a, CatalogFrame const& b,
                     int tolerance = 3) {
  return std::abs(a.x - b.x) <= tolerance &&
         std::abs(a.y - b.y) <= tolerance &&
         std::abs(a.client_width - b.client_width) <= tolerance &&
         std::abs(a.client_height - b.client_height) <= tolerance;
}

void ApplyCatalogFrame(HWND hwnd, int x, int y, int client_width,
                       int client_height) {
  int outer_width = 0;
  int outer_height = 0;
  apptraverse::ui::windows::ClientSizeToOuter(client_width, client_height,
                                              outer_width, outer_height);
  CHECK(SetWindowPos(hwnd, nullptr, x, y, outer_width, outer_height,
                     SWP_NOZORDER | SWP_NOACTIVATE) != 0);
  SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
}

bool ControlEnabled(HWND parent, std::uint32_t control_id) {
  HWND const child = GetDlgItem(parent, static_cast<int>(control_id));
  return child != nullptr && IsWindowEnabled(child) == TRUE;
}

struct ActiveDemoProcess {
  PROCESS_INFORMATION pi{};

  explicit ActiveDemoProcess(PROCESS_INFORMATION in) : pi(in) {
    ActiveDemos().Add(&pi);
  }

  ActiveDemoProcess(ActiveDemoProcess const&) = delete;
  ActiveDemoProcess& operator=(ActiveDemoProcess const&) = delete;

  ~ActiveDemoProcess() { TerminateProcessHandle(pi); }

  void Release() {
    ActiveDemos().Remove(&pi);
    if (pi.hProcess != nullptr) {
      CloseHandle(pi.hProcess);
      pi.hProcess = nullptr;
    }
  }
};

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
  if (!WaitUntil(std::chrono::seconds{15}, [&] {
        return ListRowsMatch(main, expected);
      })) {
    HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
    LRESULT const count = list ? SendMessageW(list, LB_GETCOUNT, 0, 0) : LB_ERR;
    std::cerr << "ExpectListRows mismatch: expected " << expected.size()
              << " rows, list count=" << count << '\n';
    if (list && count != LB_ERR) {
      for (int i = 0; i < count; ++i) {
        std::wcerr << L"  [" << i << L"] " << ListBoxItem(list, i) << L'\n';
      }
    }
    ActiveDemos().TerminateAll();
    std::exit(1);
  }
}

void ExpectChrome(HWND main, wchar_t const* add, wchar_t const* open,
                  wchar_t const* remove, wchar_t const* language) {
  // Native controls are updated sequentially. Observing Add alone does not
  // mean that the same publication has reached the other three HWNDs yet.
  bool const ready = WaitUntil(std::chrono::seconds{10}, [&] {
    return GetControlText(main, kAddControlId) == add &&
           GetControlText(main, kOpenControlId) == open &&
           GetControlText(main, kDeleteControlId) == remove &&
           GetControlText(main, kLanguageControlId) == language;
  });
  if (!ready) {
    std::wcerr << L"Chrome did not converge: Add=["
               << GetControlText(main, kAddControlId) << L"], Open=["
               << GetControlText(main, kOpenControlId) << L"], Delete=["
               << GetControlText(main, kDeleteControlId) << L"], Language=["
               << GetControlText(main, kLanguageControlId) << L"]\n";
  }
  CHECK(ready);
}

void ExpectChromeEn(HWND main) {
  ExpectChrome(main, L"Add", L"Open", L"Delete", L"RU");
}

void ExpectChromeRu(HWND main) {
  ExpectChrome(main, L"Добавить", L"Открыть", L"Удалить", L"EN");
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
  pi.hProcess = nullptr;
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

ae::ObjId CatalogRootObjId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

ae::ObjId CatalogMainWindowObjId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::MainWindow)};
}

ae::ObjId CatalogMainWindowPresenterObjId() {
  return ae::ObjId{window_catalog_demo::ToObjId(
      window_catalog_demo::ObjId::MainWindowPresenter)};
}

ae::ObjId CatalogListObjId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::CatalogList)};
}

ae::ObjId CatalogLocaleObjId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Locale)};
}

std::filesystem::path NegativeStateDir(char const* suffix) {
  auto dir = std::filesystem::temp_directory_path() / "apptraverse_test" /
             ("window_catalog_win32_acceptance_" + std::string{suffix});
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

void CopyStateDir(std::filesystem::path const& from,
                  std::filesystem::path const& to) {
  std::error_code ec;
  std::filesystem::copy(from, to,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing,
                        ec);
  CHECK(!ec);
}

std::string ReadAllBytes(std::filesystem::path const& path) {
  HANDLE file =
      CreateFileW(path.wstring().c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(file != INVALID_HANDLE_VALUE);
  DWORD size = GetFileSize(file, nullptr);
  CHECK(size != INVALID_FILE_SIZE);
  std::string out(size, '\0');
  DWORD read = 0;
  CHECK(ReadFile(file, out.data(), size, &read, nullptr) != 0);
  CloseHandle(file);
  out.resize(read);
  return out;
}

std::size_t CountNeedle(std::string const& hay, char const* needle) {
  std::size_t n = 0;
  for (std::size_t pos = 0;;) {
    pos = hay.find(needle, pos);
    if (pos == std::string::npos) {
      break;
    }
    ++n;
    ++pos;
  }
  return n;
}

struct ChildRunResult {
  DWORD exit_code = 0;
  std::string stderr_text;
};

ChildRunResult RunLoadOnlyCaptureStderr(
    std::filesystem::path const& exe,
    std::filesystem::path const& state_dir,
    DWORD timeout_ms = 30000) {
  auto err_path = state_dir;
  err_path += L".stderr.txt";
  std::error_code ec;
  std::filesystem::remove(err_path, ec);

  std::wstring cmd = L"\"" + exe.wstring() + L"\" --state-dir \"" +
                     state_dir.wstring() + L"\"";
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE errf = CreateFileW(err_path.wstring().c_str(), GENERIC_WRITE,
                            FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(errf != INVALID_HANDLE_VALUE);
  HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                           OPEN_EXISTING, 0, nullptr);
  CHECK(nul != INVALID_HANDLE_VALUE);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = nul;
  si.hStdOutput = nul;
  si.hStdError = errf;
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(exe.wstring().c_str(), cmd_buf.data(), nullptr,
                           nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &si, &pi);
  CloseHandle(errf);
  CloseHandle(nul);
  CHECK(ok);
  CloseHandle(pi.hThread);

  DWORD wait = WaitForSingleObject(pi.hProcess, timeout_ms);
  if (wait == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CHECK(false);
  }
  ChildRunResult result;
  GetExitCodeProcess(pi.hProcess, &result.exit_code);
  CloseHandle(pi.hProcess);
  result.stderr_text = ReadAllBytes(err_path);
  std::filesystem::remove(err_path, ec);
  return result;
}

void ExpectFatalLoadOnly(ChildRunResult const& result, char const* needle) {
  CHECK(result.exit_code != 0);
  if (result.stderr_text.find(needle) == std::string::npos) {
    std::cerr << "load-only stderr was:\n" << result.stderr_text << '\n';
    std::exit(1);
  }
  CHECK(CountNeedle(result.stderr_text, needle) >= 1);
}

void RegisterCatalogTypes() {
  EnsureObjectRegistration();
  EnsureWindowCatalogGraphRegistration();
}

void WriteProfileMissingMainWindow(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->main_window = CatalogWindow::ptr::Declare(
      ae::CreateWith{domain}.with_id(ae::ObjId{}));
  if (catalog->base.is_valid()) {
    if (!catalog->base.is_loaded()) {
      catalog->base.Load();
    }
    auto& base_catalog = static_cast<WindowCatalog&>(*catalog->base.operator->());
    base_catalog.main_window = CatalogWindow::ptr::Declare(
        ae::CreateWith{domain}.with_id(ae::ObjId{}));
    catalog->base.Save();
  }
  ae::DomainGraph graph{&domain};
  CHECK(graph.SaveRoot(catalog, CatalogRootObjId()).IsOk());
}

void AssertMainWindowRefAbsent(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  CHECK(!catalog->main_window.is_valid());
}

void WriteProfileMissingPresenter(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  ae::Ptr<CatalogWindow> window = catalog->main_window.Load();
  window->presenter = ui::windows::WinDesktopWindowPresenter::ptr::Declare(
      ae::CreateWith{domain}.with_id(ae::ObjId{}));
  ae::DomainGraph graph{&domain};
  CHECK(graph.SaveRoot(window, CatalogMainWindowObjId()).IsOk());
}

void WriteProfileMissingPresenterObject(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  storage.Remove(CatalogMainWindowPresenterObjId());
}

void WriteProfileMissingListRef(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->list = CatalogListContainer::ptr::Declare(
      ae::CreateWith{domain}.with_id(ae::ObjId{}));
  if (catalog->base.is_valid()) {
    if (!catalog->base.is_loaded()) {
      catalog->base.Load();
    }
    auto& base_catalog = static_cast<WindowCatalog&>(*catalog->base.operator->());
    base_catalog.list = CatalogListContainer::ptr::Declare(
        ae::CreateWith{domain}.with_id(ae::ObjId{}));
    catalog->base.Save();
  }
  ae::DomainGraph graph{&domain};
  CHECK(graph.SaveRoot(catalog, CatalogRootObjId()).IsOk());
}

void AssertListRefAbsent(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->list.Load();
  CHECK(!catalog->list.is_valid());
  CHECK(!catalog->list);
}

void WriteProfileMissingListObject(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  storage.Remove(CatalogListObjId());
}

void AssertListObjectAbsent(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  CHECK(storage.Enumerate(CatalogListObjId()).empty());
}

void WriteProfileMissingLocaleRef(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->locale = ui::UiLocaleSettings::ptr::Declare(
      ae::CreateWith{domain}.with_id(ae::ObjId{}));
  if (catalog->base.is_valid()) {
    if (!catalog->base.is_loaded()) {
      catalog->base.Load();
    }
    auto& base_catalog = static_cast<WindowCatalog&>(*catalog->base.operator->());
    base_catalog.locale = ui::UiLocaleSettings::ptr::Declare(
        ae::CreateWith{domain}.with_id(ae::ObjId{}));
    catalog->base.Save();
  }
  ae::DomainGraph graph{&domain};
  CHECK(graph.SaveRoot(catalog, CatalogRootObjId()).IsOk());
}

void AssertLocaleRefAbsent(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->locale.Load();
  CHECK(!catalog->locale.is_valid());
  CHECK(!catalog->locale);
}

ae::ObjId LoadCatalogLocaleObjId(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->locale.Load();
  CHECK(catalog->locale.is_valid());
  return catalog->locale.id();
}

ae::ObjId WriteProfileMissingLocaleObject(
    std::filesystem::path const& state_dir) {
  ae::ObjId const locale_id = LoadCatalogLocaleObjId(state_dir);
  DirectoryDomainStorage storage{state_dir};
  storage.Remove(locale_id);
  return locale_id;
}

void AssertLocaleObjectAbsent(std::filesystem::path const& state_dir,
                              ae::ObjId locale_id) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  CHECK(storage.Enumerate(locale_id).empty());
}

using ChromePresenterMember = Presenter::ptr WindowCatalogPresenters::*;

ChromePresenterMember ChromePresenterMemberForSuffix(char const* suffix) {
  if (std::string{suffix} == "list_presenter") {
    return &WindowCatalogPresenters::list_presenter;
  }
  if (std::string{suffix} == "add_button_presenter") {
    return &WindowCatalogPresenters::add_button_presenter;
  }
  if (std::string{suffix} == "open_button_presenter") {
    return &WindowCatalogPresenters::open_button_presenter;
  }
  if (std::string{suffix} == "delete_button_presenter") {
    return &WindowCatalogPresenters::delete_button_presenter;
  }
  if (std::string{suffix} == "language_button_presenter") {
    return &WindowCatalogPresenters::language_button_presenter;
  }
  CHECK(false);
  return &WindowCatalogPresenters::list_presenter;
}

void WriteProfileMissingChromePresenterRef(
    std::filesystem::path const& state_dir, ChromePresenterMember member) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  Presenter::ptr const empty = Presenter::ptr::Declare(
      ae::CreateWith{domain}.with_id(ae::ObjId{}));
  catalog->presenters.*member = empty;
  if (catalog->base.is_valid()) {
    if (!catalog->base.is_loaded()) {
      catalog->base.Load();
    }
    auto& base_catalog = static_cast<WindowCatalog&>(*catalog->base.operator->());
    base_catalog.presenters.*member = empty;
    catalog->base.Save();
  }
  ae::DomainGraph graph{&domain};
  CHECK(graph.SaveRoot(catalog, CatalogRootObjId()).IsOk());
}

void AssertChromePresenterRefAbsent(std::filesystem::path const& state_dir,
                                    ChromePresenterMember member) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  (catalog->presenters.*member).Load();
  CHECK(!(catalog->presenters.*member).is_valid());
  CHECK(!(catalog->presenters.*member));
}

ae::ObjId LoadAddButtonPresenterObjId(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->presenters.add_button_presenter.Load();
  CHECK(catalog->presenters.add_button_presenter.is_valid());
  return catalog->presenters.add_button_presenter.id();
}

ae::ObjId WriteProfileMissingAddButtonPresenterObject(
    std::filesystem::path const& state_dir) {
  ae::ObjId const presenter_id = LoadAddButtonPresenterObjId(state_dir);
  DirectoryDomainStorage storage{state_dir};
  storage.Remove(presenter_id);
  return presenter_id;
}

void AssertPresenterObjectAbsent(std::filesystem::path const& state_dir,
                                 ae::ObjId presenter_id) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  CHECK(storage.Enumerate(presenter_id).empty());
}

void ExpectFatalLoadOnlyOrLoadApplication(ChildRunResult const& result,
                                          char const* primary_needle) {
  bool const primary =
      result.stderr_text.find(primary_needle) != std::string::npos;
  bool const load_failed =
      result.stderr_text.find("fatal: LoadApplication failed") !=
      std::string::npos;
  CHECK(result.exit_code != 0);
  CHECK(primary || load_failed);
  if (!primary && !load_failed) {
    std::cerr << "load-only stderr was:\n" << result.stderr_text << '\n';
    std::exit(1);
  }
}

using ChromeButtonMember = ae::ObjPtr<ui::UiPushButton> WindowCatalogButtons::*;

ChromeButtonMember ChromeButtonMemberForSuffix(char const* suffix) {
  if (std::string{suffix} == "add_button") {
    return &WindowCatalogButtons::add_button;
  }
  if (std::string{suffix} == "open_button") {
    return &WindowCatalogButtons::open_button;
  }
  if (std::string{suffix} == "delete_button") {
    return &WindowCatalogButtons::delete_button;
  }
  if (std::string{suffix} == "language_button") {
    return &WindowCatalogButtons::language_button;
  }
  CHECK(false);
  return &WindowCatalogButtons::add_button;
}

void WriteProfileMissingChromeButtonRef(std::filesystem::path const& state_dir,
                                        ChromeButtonMember member) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  ui::UiPushButton::ptr const empty = ui::UiPushButton::ptr::Declare(
      ae::CreateWith{domain}.with_id(ae::ObjId{}));
  catalog->buttons.*member = empty;
  if (catalog->base.is_valid()) {
    if (!catalog->base.is_loaded()) {
      catalog->base.Load();
    }
    auto& base_catalog = static_cast<WindowCatalog&>(*catalog->base.operator->());
    base_catalog.buttons.*member = empty;
    catalog->base.Save();
  }
  ae::DomainGraph graph{&domain};
  CHECK(graph.SaveRoot(catalog, CatalogRootObjId()).IsOk());
}

void AssertChromeButtonRefAbsent(std::filesystem::path const& state_dir,
                                 ChromeButtonMember member) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  (catalog->buttons.*member).Load();
  CHECK(!(catalog->buttons.*member).is_valid());
  CHECK(!(catalog->buttons.*member));
}

ae::ObjId LoadAddButtonObjId(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->buttons.add_button.Load();
  CHECK(catalog->buttons.add_button.is_valid());
  return catalog->buttons.add_button.id();
}

ae::ObjId WriteProfileMissingAddButtonObject(
    std::filesystem::path const& state_dir) {
  ae::ObjId const button_id = LoadAddButtonObjId(state_dir);
  DirectoryDomainStorage storage{state_dir};
  storage.Remove(button_id);
  return button_id;
}

void RunRequiredChromeButtonNegativeTests(
    std::filesystem::path const& good_state,
    std::filesystem::path const& load_only) {
  char const* const kRefSuffixes[] = {"add_button", "open_button", "delete_button",
                                      "language_button"};
  for (char const* suffix : kRefSuffixes) {
    std::string const dir_name =
        std::string{"missing_chrome_"} + suffix + "_ref";
    auto const dir = NegativeStateDir(dir_name.c_str());
    CopyStateDir(good_state, dir);
    ChromeButtonMember const member = ChromeButtonMemberForSuffix(suffix);
    WriteProfileMissingChromeButtonRef(dir, member);
    AssertChromeButtonRefAbsent(dir, member);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnly(result,
                        "fatal: WindowCatalog required chrome button missing");
  }

  {
    auto const dir = NegativeStateDir("missing_chrome_add_button_object");
    CopyStateDir(good_state, dir);
    ae::ObjId const button_id = WriteProfileMissingAddButtonObject(dir);
    AssertPresenterObjectAbsent(dir, button_id);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnlyOrLoadApplication(
        result, "fatal: WindowCatalog required chrome button missing");
  }
}

using LocalizedTextMember =
    ae::ObjPtr<ui::UiLocalizationString> WindowCatalogTexts::*;

LocalizedTextMember LocalizedTextMemberForSuffix(char const* suffix) {
  if (std::string{suffix} == "label_add") {
    return &WindowCatalogTexts::label_add;
  }
  if (std::string{suffix} == "label_open") {
    return &WindowCatalogTexts::label_open;
  }
  if (std::string{suffix} == "label_delete") {
    return &WindowCatalogTexts::label_delete;
  }
  if (std::string{suffix} == "label_language") {
    return &WindowCatalogTexts::label_language;
  }
  if (std::string{suffix} == "window_title_prefix") {
    return &WindowCatalogTexts::window_title_prefix;
  }
  CHECK(false);
  return &WindowCatalogTexts::label_add;
}

void WriteProfileMissingLocalizedTextRef(std::filesystem::path const& state_dir,
                                         LocalizedTextMember member) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  ui::UiLocalizationString::ptr const empty =
      ui::UiLocalizationString::ptr::Declare(
          ae::CreateWith{domain}.with_id(ae::ObjId{}));
  catalog->texts.*member = empty;
  if (catalog->base.is_valid()) {
    if (!catalog->base.is_loaded()) {
      catalog->base.Load();
    }
    auto& base_catalog = static_cast<WindowCatalog&>(*catalog->base.operator->());
    base_catalog.texts.*member = empty;
    catalog->base.Save();
  }
  ae::DomainGraph graph{&domain};
  CHECK(graph.SaveRoot(catalog, CatalogRootObjId()).IsOk());
}

void AssertLocalizedTextRefAbsent(std::filesystem::path const& state_dir,
                                  LocalizedTextMember member) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  (catalog->texts.*member).Load();
  CHECK(!(catalog->texts.*member).is_valid());
  CHECK(!(catalog->texts.*member));
}

ae::ObjId LoadLabelAddObjId(std::filesystem::path const& state_dir) {
  RegisterCatalogTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->texts.label_add.Load();
  CHECK(catalog->texts.label_add.is_valid());
  return catalog->texts.label_add.id();
}

ae::ObjId WriteProfileMissingLabelAddObject(
    std::filesystem::path const& state_dir) {
  ae::ObjId const text_id = LoadLabelAddObjId(state_dir);
  DirectoryDomainStorage storage{state_dir};
  storage.Remove(text_id);
  return text_id;
}

void RunRequiredLocalizedTextNegativeTests(
    std::filesystem::path const& good_state,
    std::filesystem::path const& load_only) {
  char const* const kRefSuffixes[] = {
      "label_add",  "label_open",         "label_delete",
      "label_language", "window_title_prefix",
  };
  for (char const* suffix : kRefSuffixes) {
    std::string const dir_name =
        std::string{"missing_localized_text_"} + suffix + "_ref";
    auto const dir = NegativeStateDir(dir_name.c_str());
    CopyStateDir(good_state, dir);
    LocalizedTextMember const member = LocalizedTextMemberForSuffix(suffix);
    WriteProfileMissingLocalizedTextRef(dir, member);
    AssertLocalizedTextRefAbsent(dir, member);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnly(result,
                        "fatal: WindowCatalog required localized text missing");
  }

  {
    auto const dir = NegativeStateDir("missing_localized_text_label_add_object");
    CopyStateDir(good_state, dir);
    ae::ObjId const text_id = WriteProfileMissingLabelAddObject(dir);
    AssertPresenterObjectAbsent(dir, text_id);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnlyOrLoadApplication(
        result, "fatal: WindowCatalog required localized text missing");
  }
}

void RunRequiredChromePresenterNegativeTests(
    std::filesystem::path const& good_state,
    std::filesystem::path const& load_only) {
  char const* const kRefSuffixes[] = {
      "list_presenter",          "add_button_presenter",
      "open_button_presenter",   "delete_button_presenter",
      "language_button_presenter",
  };
  for (char const* suffix : kRefSuffixes) {
    std::string const dir_name = std::string{"missing_chrome_"} + suffix + "_ref";
    auto const dir = NegativeStateDir(dir_name.c_str());
    CopyStateDir(good_state, dir);
    ChromePresenterMember const member = ChromePresenterMemberForSuffix(suffix);
    WriteProfileMissingChromePresenterRef(dir, member);
    AssertChromePresenterRefAbsent(dir, member);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnly(result,
                        "fatal: WindowCatalog required chrome presenter missing");
  }

  {
    auto const dir = NegativeStateDir("missing_chrome_add_button_presenter_object");
    CopyStateDir(good_state, dir);
    ae::ObjId const presenter_id =
        WriteProfileMissingAddButtonPresenterObject(dir);
    AssertPresenterObjectAbsent(dir, presenter_id);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnlyOrLoadApplication(
        result, "fatal: WindowCatalog required chrome presenter missing");
  }
}

void RunRequiredListLocaleNegativeTests(
    std::filesystem::path const& good_state,
    std::filesystem::path const& load_only) {
  {
    auto const dir = NegativeStateDir("missing_list_ref");
    CopyStateDir(good_state, dir);
    WriteProfileMissingListRef(dir);
    AssertListRefAbsent(dir);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnly(result, "fatal: WindowCatalog required list missing");
  }

  {
    auto const dir = NegativeStateDir("missing_list_object");
    CopyStateDir(good_state, dir);
    WriteProfileMissingListObject(dir);
    AssertListObjectAbsent(dir);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnlyOrLoadApplication(
        result, "fatal: WindowCatalog required list missing");
  }

  {
    auto const dir = NegativeStateDir("missing_locale_ref");
    CopyStateDir(good_state, dir);
    WriteProfileMissingLocaleRef(dir);
    AssertLocaleRefAbsent(dir);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnly(result, "fatal: WindowCatalog required locale missing");
  }

  {
    auto const dir = NegativeStateDir("missing_locale_object");
    CopyStateDir(good_state, dir);
    ae::ObjId const locale_id = WriteProfileMissingLocaleObject(dir);
    AssertLocaleObjectAbsent(dir, locale_id);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnlyOrLoadApplication(
        result, "fatal: WindowCatalog required locale missing");
  }
}

void RunRequiredMainWindowNegativeTests(
    std::filesystem::path const& good_state,
    std::filesystem::path const& load_only) {
  {
    auto const empty_dir = NegativeStateDir("missing_root");
    auto const result = RunLoadOnlyCaptureStderr(load_only, empty_dir);
    ExpectFatalLoadOnly(result, "fatal: LoadApplication failed");
  }

  {
    auto const dir = NegativeStateDir("missing_main_window");
    CopyStateDir(good_state, dir);
    WriteProfileMissingMainWindow(dir);
    AssertMainWindowRefAbsent(dir);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnly(result,
                        "fatal: LoadPresentedWindow required window missing");
  }

  {
    auto const dir = NegativeStateDir("missing_presenter_ref");
    CopyStateDir(good_state, dir);
    WriteProfileMissingPresenter(dir);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnly(result,
                        "fatal: LoadPresentedWindow required presenter missing");
  }

  {
    auto const dir = NegativeStateDir("missing_presenter_object");
    CopyStateDir(good_state, dir);
    WriteProfileMissingPresenterObject(dir);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    bool const presenter_missing =
        result.stderr_text.find(
            "fatal: LoadPresentedWindow required presenter missing") !=
        std::string::npos;
    bool const load_failed =
        result.stderr_text.find("fatal: LoadApplication failed") !=
        std::string::npos;
    CHECK(result.exit_code != 0);
    CHECK(presenter_missing || load_failed);
    if (!presenter_missing && !load_failed) {
      std::cerr << "missing presenter object stderr was:\n"
                << result.stderr_text << '\n';
      std::exit(1);
    }
  }
}

std::filesystem::path RunRestartPersistenceAcceptance() {
  auto const state_dir = FreshStateDir("window_catalog_win32_acceptance");
  auto const demo = DemoExe("win32_window_catalog_demo.exe");
  auto const load_only = DemoExe("win32_window_catalog_demo_load_only.exe");
  CHECK(std::filesystem::exists(demo));
  CHECK(std::filesystem::exists(load_only));

  ActiveDemoProcess session{StartDemo(demo, state_dir)};
  DWORD const pid = session.pi.dwProcessId;
  HWND main = WaitCatalogMainWindow(pid, std::chrono::seconds{30});
  CHECK(main != nullptr);
  WaitChromeReady(main);

  CHECK(WaitUntil(std::chrono::seconds{5}, [&] {
    HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
    return list && SendMessageW(list, LB_GETCOUNT, 0, 0) == 0;
  }));
  CHECK(TopLevelWindowsForPid(pid).size() == 1);
  CHECK(ControlEnabled(main, kAddControlId));
  CHECK(!ControlEnabled(main, kOpenControlId));
  CHECK(!ControlEnabled(main, kDeleteControlId));

  for (int i = 0; i < 2; ++i) {
    ClickControl(main, kAddControlId);
    CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
      HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
      return list && SendMessageW(list, LB_GETCOUNT, 0, 0) == i + 1;
    }));
  }
  ExpectListRows(main, {L"Window 1", L"Window 2"});
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return TopLevelWindowsForPid(pid).size() == 3;
  }));

  HWND child1 = FindTopLevelWithTitle(pid, L"Window 1");
  HWND child2 = FindTopLevelWithTitle(pid, L"Window 2");
  CHECK(child1 != nullptr);
  CHECK(child2 != nullptr);

  ApplyCatalogFrame(child1, 280, 180, 700, 430);
  CatalogFrame const saved_frame = ReadCatalogFrame(child1);
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return FramesNearEqual(ReadCatalogFrame(child1), saved_frame);
  }));

  SendMessageW(child1, WM_CLOSE, 0, 0);
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return FindTopLevelWithTitle(pid, L"Window 1") == nullptr;
  }));
  ExpectListRows(main, {L"Window 1", L"Window 2"});
  CHECK(FindTopLevelWithTitle(pid, L"Window 2") == child2);

  SelectListIndex(main, 0);
  CHECK(WaitUntil(std::chrono::seconds{5}, [&] {
    return ControlEnabled(main, kOpenControlId);
  }));
  ClickControl(main, kOpenControlId);
  HWND child1_reopened = nullptr;
  CHECK(WaitUntil(std::chrono::seconds{15}, [&] {
    child1_reopened = FindTopLevelWithTitle(pid, L"Window 1");
    return child1_reopened != nullptr;
  }));
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return FramesNearEqual(ReadCatalogFrame(child1_reopened), saved_frame);
  }));

  ClickControl(main, kOpenControlId);
  CHECK(WaitUntil(std::chrono::seconds{5}, [&] {
    return FindTopLevelWithTitle(pid, L"Window 1") == child1_reopened;
  }));
  CHECK(FramesNearEqual(ReadCatalogFrame(child1_reopened), saved_frame));

  SendMessageW(child1_reopened, WM_CLOSE, 0, 0);
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return FindTopLevelWithTitle(pid, L"Window 1") == nullptr;
  }));
  CHECK(FindTopLevelWithTitle(pid, L"Window 2") == child2);

  CloseProcessGracefully(main, session.pi);
  session.Release();

  std::vector<ae::ObjId> saved_holder_ids;
  ProbeProfileListRows(state_dir, 2, &saved_holder_ids);
  CHECK(saved_holder_ids.size() == 2);

  {
    ActiveDemoProcess load_session{StartDemo(load_only, state_dir)};
    DWORD const load_pid = load_session.pi.dwProcessId;
    main = WaitCatalogMainWindow(load_pid, std::chrono::seconds{30});
    CHECK(main != nullptr);
    WaitChromeReady(main);
    ExpectListRows(main, {L"Window 1", L"Window 2"});
    CHECK(FindTopLevelWithTitle(load_pid, L"Window 1") == nullptr);
    CHECK(FindTopLevelWithTitle(load_pid, L"Window 2") != nullptr);
    CHECK(TopLevelWindowsForPid(load_pid).size() == 2);

    SelectListIndex(main, 0);
    CHECK(WaitUntil(std::chrono::seconds{5}, [&] {
      return ControlEnabled(main, kOpenControlId);
    }));
    ClickControl(main, kOpenControlId);
    HWND child1_loaded = nullptr;
    CHECK(WaitUntil(std::chrono::seconds{15}, [&] {
      child1_loaded = FindTopLevelWithTitle(load_pid, L"Window 1");
      return child1_loaded != nullptr;
    }));
    CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
      return FramesNearEqual(ReadCatalogFrame(child1_loaded), saved_frame);
    }));
    CHECK(ControlEnabled(main, kAddControlId));

    ClickControl(main, kAddControlId);
    CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
      HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
      return list && SendMessageW(list, LB_GETCOUNT, 0, 0) == 3;
    }));
    ExpectListRows(main, {L"Window 1", L"Window 2", L"Window 3"});
    CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
      return FindTopLevelWithTitle(load_pid, L"Window 3") != nullptr;
    }));

    CloseProcessGracefully(main, load_session.pi);
    load_session.Release();
  }

  std::vector<ae::ObjId> const original_holder_ids = saved_holder_ids;
  std::vector<ae::ObjId> reloaded_holder_ids;
  ProbeProfileListRows(state_dir, 3, &reloaded_holder_ids);
  CHECK(reloaded_holder_ids.size() == 3);
  CHECK(original_holder_ids.size() == 2);
  CHECK(reloaded_holder_ids[0] == original_holder_ids[0]);
  CHECK(reloaded_holder_ids[1] == original_holder_ids[1]);
  CHECK(reloaded_holder_ids[2] != original_holder_ids[0]);
  CHECK(reloaded_holder_ids[2] != original_holder_ids[1]);

  {
    ActiveDemoProcess restart_session{StartDemo(demo, state_dir)};
    DWORD const restart_pid = restart_session.pi.dwProcessId;
    main = WaitCatalogMainWindow(restart_pid, std::chrono::seconds{30});
    CHECK(main != nullptr);
    WaitChromeReady(main);
    ExpectListRows(main, {L"Window 1", L"Window 2", L"Window 3"});
    CHECK(WaitUntil(std::chrono::seconds{20}, [&] {
      return TopLevelWindowsForPid(restart_pid).size() >= 3;
    }));
    CloseProcessGracefully(main, restart_session.pi);
    restart_session.Release();
  }
  return state_dir;
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
  ExpectChromeRu(main);
  ExpectListRows(main,
                 {L"\x041E\x043A\x043D\x043E 1", L"\x041E\x043A\x043D\x043E 2",
                  L"\x041E\x043A\x043D\x043E 3"});

  ClickControl(main, kLanguageControlId);
  EnsureCatalogMainWindow(pi.dwProcessId, main);
  ExpectChromeEn(main);

  CloseProcessGracefully(main, pi);
}

void RelocateStoredLayerVersion0To1(std::filesystem::path const& state_dir,
                                    ae::ObjId obj_id,
                                    std::uint32_t class_id) {
  ae::DomainQuery const query_v0{obj_id, class_id, 0};
  ae::DomainQuery const query_v1{obj_id, class_id, 1};
  auto const path_v0 = DirectoryDomainStorageLayerPath(state_dir, query_v0);
  auto const path_v1 = DirectoryDomainStorageLayerPath(state_dir, query_v1);
  CHECK(std::filesystem::exists(path_v0));
  std::error_code ec;
  std::filesystem::create_directories(path_v1.parent_path(), ec);
  CHECK(!ec);
  std::filesystem::rename(path_v0, path_v1, ec);
  CHECK(!ec);
  DirectoryDomainStorage storage{state_dir};
  CHECK(storage.Load(query_v0).result == ae::DomainLoadResult::kEmpty);
  CHECK(storage.Load(query_v1).result == ae::DomainLoadResult::kLoaded);
}

void RelocateStoredClassSchemaLayers(
    std::filesystem::path const& state_dir, ae::ObjId obj_id,
    std::uint32_t class_id,
    std::array<std::uint8_t, 3> const& from_schema,
    std::array<std::uint8_t, 3> const& to_schema) {
  std::size_t relocated = 0;
  for (std::size_t i = 0; i < from_schema.size(); ++i) {
    ae::DomainQuery const from_q{obj_id, class_id, from_schema[i]};
    auto const from_path = DirectoryDomainStorageLayerPath(state_dir, from_q);
    if (!std::filesystem::exists(from_path)) {
      continue;
    }
    ae::DomainQuery const to_q{obj_id, class_id, to_schema[i]};
    auto const to_path = DirectoryDomainStorageLayerPath(state_dir, to_q);
    CHECK(!std::filesystem::exists(to_path));
    std::error_code ec;
    std::filesystem::create_directories(to_path.parent_path(), ec);
    CHECK(!ec);
    std::filesystem::rename(from_path, to_path, ec);
    CHECK(!ec);
    ++relocated;
  }
  CHECK(relocated > 0);
  DirectoryDomainStorage storage{state_dir};
  for (std::uint8_t schema : from_schema) {
    ae::DomainQuery const q{obj_id, class_id, schema};
    CHECK(storage.Load(q).result == ae::DomainLoadResult::kEmpty);
  }
  for (std::size_t i = 0; i < to_schema.size(); ++i) {
    ae::DomainQuery const to_q{obj_id, class_id, to_schema[i]};
    if (!std::filesystem::exists(
            DirectoryDomainStorageLayerPath(state_dir, to_q))) {
      continue;
    }
    CHECK(storage.Load(to_q).result == ae::DomainLoadResult::kLoaded);
  }
}

void ExpectUnsupportedSchemaLoadOnly(char const* scenario,
                                     ChildRunResult const& result,
                                     bool allow_presenter_boundary) {
  bool const load_failed =
      result.stderr_text.find("fatal: LoadApplication failed") !=
      std::string::npos;
  bool const presenter_missing =
      allow_presenter_boundary &&
      result.stderr_text.find(
          "fatal: LoadPresentedWindow required presenter missing") !=
          std::string::npos;
  bool const ok = result.exit_code != 0 && (load_failed || presenter_missing);
  if (!ok) {
    std::cerr << "unsupported-schema scenario: " << scenario << '\n'
              << "child exit: dec=" << result.exit_code << " hex=0x"
              << std::hex << result.exit_code << std::dec << '\n'
              << "load_failed=" << load_failed
              << " presenter_missing=" << presenter_missing << '\n'
              << "captured stderr:\n"
              << result.stderr_text << '\n';
  }
  CHECK(result.exit_code != 0);
  CHECK(load_failed || presenter_missing);
}

std::uint32_t UnknownStoredClassIdForTests() {
  constexpr std::uint32_t kUnknown = 0xC1D00001u;
  CHECK(kUnknown != ae::Obj::kClassId);
  return kUnknown;
}

bool EnumerateContainsClass(DirectoryDomainStorage& storage, ae::ObjId obj_id,
                            std::uint32_t class_id) {
  for (auto const stored : storage.Enumerate(obj_id)) {
    if (stored == class_id) {
      return true;
    }
  }
  return false;
}

void RelocateWindowCatalogRootSupportedSchemaAway(
    std::filesystem::path const& state_dir) {
  ae::ObjId const root = CatalogRootObjId();
  RelocateStoredClassSchemaLayers(
      state_dir, root, WindowCatalog::kClassId, {0, 1, 2}, {3, 4, 5});
  DirectoryDomainStorage storage{state_dir};
  CHECK(EnumerateContainsClass(storage, root, WindowCatalog::kClassId));
}

void CopyStoredLayer(std::filesystem::path const& state_dir,
                     ae::DomainQuery const& from, ae::DomainQuery const& to) {
  auto const from_path = DirectoryDomainStorageLayerPath(state_dir, from);
  auto const to_path = DirectoryDomainStorageLayerPath(state_dir, to);
  CHECK(std::filesystem::exists(from_path));
  std::error_code ec;
  std::filesystem::create_directories(to_path.parent_path(), ec);
  CHECK(!ec);
  std::filesystem::copy_file(from_path, to_path, ec);
  CHECK(!ec);
}

void AddUnknownStoredClassLayerAlongsideRoot(
    std::filesystem::path const& state_dir) {
  std::uint32_t const unknown = UnknownStoredClassIdForTests();
  ae::ObjId const root = CatalogRootObjId();
  ae::DomainQuery const src{root, WindowCatalog::kClassId, 0};
  ae::DomainQuery const dst{root, unknown, 0};
  CopyStoredLayer(state_dir, src, dst);
  DirectoryDomainStorage storage{state_dir};
  CHECK(EnumerateContainsClass(storage, root, WindowCatalog::kClassId));
  CHECK(EnumerateContainsClass(storage, root, unknown));
}

void RelocateStoredClassDirectory(std::filesystem::path const& state_dir,
                                  ae::ObjId obj_id,
                                  std::uint32_t from_class_id,
                                  std::uint32_t to_class_id) {
  auto const object_dir = state_dir / std::to_string(obj_id.id());
  auto const from_dir = object_dir / std::to_string(from_class_id);
  auto const to_dir = object_dir / std::to_string(to_class_id);
  CHECK(std::filesystem::exists(from_dir));
  std::error_code ec;
  std::filesystem::rename(from_dir, to_dir, ec);
  CHECK(!ec);
  DirectoryDomainStorage storage{state_dir};
  CHECK(!EnumerateContainsClass(storage, obj_id, from_class_id));
  CHECK(EnumerateContainsClass(storage, obj_id, to_class_id));
  ae::DomainQuery const moved_layer{obj_id, to_class_id, 0};
  CHECK(storage.Load(moved_layer).result == ae::DomainLoadResult::kLoaded);
}

void RunRequiredUnknownClassIdNegativeTests(
    std::filesystem::path const& good_state,
    std::filesystem::path const& load_only) {
  {
    auto const dir = NegativeStateDir("unknown_class_id_alongside_root");
    CopyStateDir(good_state, dir);
    AddUnknownStoredClassLayerAlongsideRoot(dir);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectUnsupportedSchemaLoadOnly("unknown_class_id_alongside_root", result,
                                    false);
  }

  {
    auto const dir = NegativeStateDir("unknown_class_id_main_window_presenter");
    CopyStateDir(good_state, dir);
    RelocateStoredClassDirectory(
        dir, CatalogMainWindowPresenterObjId(),
        ui::windows::WinDesktopWindowPresenter::kClassId,
        UnknownStoredClassIdForTests());
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectUnsupportedSchemaLoadOnly(
        "unknown_class_id_main_window_presenter", result, true);
  }
}

void RunRequiredUnsupportedSchemaNegativeTests(
    std::filesystem::path const& good_state,
    std::filesystem::path const& load_only) {
  {
    auto const dir = NegativeStateDir("unsupported_schema_window_catalog_root");
    CopyStateDir(good_state, dir);
    RelocateWindowCatalogRootSupportedSchemaAway(dir);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectUnsupportedSchemaLoadOnly(
        "unsupported_schema_window_catalog_root", result, false);
  }

  {
    auto const dir =
        NegativeStateDir("unsupported_schema_main_window_presenter");
    CopyStateDir(good_state, dir);
    RelocateStoredLayerVersion0To1(
        dir, CatalogMainWindowPresenterObjId(),
        ui::windows::WinDesktopWindowPresenter::kClassId);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectUnsupportedSchemaLoadOnly(
        "unsupported_schema_main_window_presenter", result, true);
  }
}

void RunAcceptanceAndRequiredMainWindowNegatives() {
  auto const good_state = RunRestartPersistenceAcceptance();
  auto const load_only = DemoExe("win32_window_catalog_demo_load_only.exe");
  RunRequiredMainWindowNegativeTests(good_state, load_only);
  RunRequiredListLocaleNegativeTests(good_state, load_only);
  RunRequiredChromePresenterNegativeTests(good_state, load_only);
  RunRequiredChromeButtonNegativeTests(good_state, load_only);
  RunRequiredLocalizedTextNegativeTests(good_state, load_only);
  RunRequiredUnsupportedSchemaNegativeTests(good_state, load_only);
  RunRequiredUnknownClassIdNegativeTests(good_state, load_only);
  RunLanguageAndFocusAcceptance();
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunAcceptanceAndRequiredMainWindowNegatives();
  std::cout << "window_catalog_win32_acceptance_test OK\n";
  return 0;
}
