#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "embedded_distilled_window_catalog_supply.h"

#include "window_catalog_actions.h"
#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_list_row_text.h"
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

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      ActiveDemos().TerminateAll();                                    \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":" \
                << __LINE__ << '\n';                                   \
      std::exit(1);                                                    \
    }                                                                  \
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
  return std::filesystem::path{APPTRAVERSE_TEST_DEMO_DIR} / name;
}

std::filesystem::path RuntimeDemoExe() {
#if defined(APPTRAVERSE_TEST_DEBUG_BOOTSTRAP)
  return DemoExe("win32_window_catalog_demo_load_only.exe");
#else
  // Release acceptance must exercise the shipped application itself.
  return DemoExe("win32_window_catalog_demo.exe");
#endif
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
  return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance &&
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
  auto dir = std::filesystem::temp_directory_path() / "apptraverse_test" / tag;
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

void ActivateTestWindow(HWND parent) {
  SetForegroundWindow(parent);
  if (GetForegroundWindow() != parent) {
    // Windows may deny programmatic foreground activation to a background test
    // process. Establish the same precondition as a user clicking the title bar;
    // keep the focus assertion, and do not alter the application focus policy.
    RECT frame{};
    CHECK(GetWindowRect(parent, &frame));
    auto const x = frame.left + (frame.right - frame.left) / 2;
    auto const y = frame.top + GetSystemMetrics(SM_CYFRAME) +
                   GetSystemMetrics(SM_CYCAPTION) / 2;
    CHECK(SetCursorPos(x, y));
    INPUT click[2]{};
    click[0].type = click[1].type = INPUT_MOUSE;
    click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    CHECK(SendInput(2, click, sizeof(INPUT)) == 2);
  }
  CHECK(WaitUntil(std::chrono::seconds{5},
                  [&] { return GetForegroundWindow() == parent; }));
}

void ClickControl(HWND parent, std::uint32_t control_id) {
  HWND const child = GetDlgItem(parent, static_cast<int>(control_id));
  CHECK(child != nullptr);
  ActivateTestWindow(parent);
  SendMessageW(parent, WM_COMMAND, MAKEWPARAM(control_id, BN_CLICKED),
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
  SendMessageW(list, LB_GETTEXT, index, reinterpret_cast<LPARAM>(buf.data()));
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
  CHECK(WaitUntil(std::chrono::seconds{20},
                  [&] { return CountChildClass(main, L"Button") >= 4; }));
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
  SendMessageW(main, WM_COMMAND, MAKEWPARAM(kListControlId, LBN_SELCHANGE),
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
  if (!WaitUntil(std::chrono::seconds{15},
                 [&] { return ListRowsMatch(main, expected); })) {
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

std::string ProbeRowPrimaryUtf8(WindowCatalog const& catalog,
                                WindowHolder const& holder,
                                int language_index) {
  return ResolveWindowCatalogHolderListRowText(holder, catalog, language_index);
}

EmbeddedDistilledGraph const& WindowCatalogEmbeddedGraph() {
  return embedded_distilled::window_catalog::kEmbeddedGraph;
}

// The profile directory is USER only; the demo reads it over embedded BACKING.
struct ProfileGraphStorage {
  explicit ProfileGraphStorage(std::filesystem::path const& state_dir)
      : backing{},
        user_profile{state_dir},
        overlay{user, backing,
                EmbeddedSupplyConstantIdSet(WindowCatalogEmbeddedGraph())} {
    CHECK(apptraverse::LoadUserStorageFile(user, user_profile));
    InstallEmbeddedDistilledBacking(backing, WindowCatalogEmbeddedGraph());
  }

  ae::RamDomainStorage backing;
  std::filesystem::path user_profile;
  ae::RamDomainStorage user;
  OverlayDomainStorage overlay;
};

void InstallEmbeddedSupplyConstantLayers(ae::RamDomainStorage& supply) {
  EmbeddedDistilledGraph const& graph = WindowCatalogEmbeddedGraph();
  std::set<ae::ObjId> const ids = EmbeddedSupplyConstantIdSet(graph);
  for (std::size_t i = 0; i < graph.layer_count; ++i) {
    EmbeddedDistilledLayerRecord const& layer = graph.layers[i];
    if (ids.count(ae::ObjId{layer.object_id}) == 0) {
      continue;
    }
    ae::ObjectData bytes(layer.bytes, layer.bytes + layer.size);
    supply.SaveData(ae::DomainQuery{ae::ObjId{layer.object_id}, layer.class_id,
                                    layer.version},
                    std::move(bytes));
  }
}

void ProbeProfileListRows(
    std::filesystem::path const& state_dir, std::size_t expected_rows,
    std::vector<ae::ObjId>* out_holder_ids = nullptr,
    std::vector<ae::ObjId> const* expected_holder_prefix = nullptr) {
  ProfileGraphStorage storage{state_dir};
  ae::Domain domain{storage.overlay};
  auto catalog = LoadApplication<WindowCatalog>(
      domain, ae::ObjId{window_catalog_demo::ToObjId(
                  window_catalog_demo::ObjId::Catalog)});
  CHECK(catalog->persisted.list.is_valid());
  CHECK(catalog->persisted.list->rows.size() == expected_rows);
  if (expected_holder_prefix != nullptr) {
    CHECK(expected_holder_prefix->size() <= expected_rows);
    for (std::size_t i = 0; i < expected_holder_prefix->size(); ++i) {
      auto* h = static_cast<WindowHolder*>(
          catalog->persisted.list->rows[i].operator->());
      CHECK(h->obj_id == (*expected_holder_prefix)[i]);
    }
  }
  if (out_holder_ids != nullptr) {
    out_holder_ids->clear();
    out_holder_ids->reserve(expected_rows);
  }
  for (std::size_t i = 0; i < expected_rows; ++i) {
    auto* h = static_cast<WindowHolder*>(
        catalog->persisted.list->rows[i].operator->());
    CHECK(h->window_number == static_cast<std::uint32_t>(i + 1));
    std::string const title = ProbeRowPrimaryUtf8(*catalog, *h, 0);
    std::string const open_suffix =
        catalog->IsHolderWindowOpen(*h) ? "" : " (Closed)";
    CHECK(title ==
          ("Window " + std::to_string(h->window_number) + open_suffix));
    if (out_holder_ids != nullptr) {
      out_holder_ids->push_back(h->obj_id);
    }
    if (h->window.is_valid() && h->window.is_loaded()) {
      CHECK(domain.Find(h->window.id()) != nullptr);
    } else if (h->window.is_valid()) {
      CHECK(domain.Find(h->window.id()) == nullptr);
    }
  }
  CHECK(catalog->persisted.next_window_number ==
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
  HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
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

ChildRunResult RunLoadOnlyCaptureStderr(std::filesystem::path const& exe,
                                        std::filesystem::path const& state_dir,
                                        char const* scenario,
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
  HANDLE errf =
      CreateFileW(err_path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                  &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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
  BOOL ok =
      CreateProcessW(exe.wstring().c_str(), cmd_buf.data(), nullptr, nullptr,
                     TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(errf);
  CloseHandle(nul);
  CHECK(ok);
  CloseHandle(pi.hThread);

  DWORD wait = WaitForSingleObject(pi.hProcess, timeout_ms);
  if (wait == WAIT_TIMEOUT) {
    std::cerr << "load_only_timeout scenario=" << scenario
              << " exe=" << exe.string() << " state_dir=" << state_dir.string()
              << '\n';
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

void ExpectUserOnlyMissingRootLayer(std::filesystem::path const& user_dir) {
  ae::RamDomainStorage user;
  auto const user_profile = user_dir;
  CHECK(apptraverse::LoadUserStorageFile(user, user_profile));
  ae::DomainQuery const query{
      CatalogRootObjId(), WindowCatalog::kClassId,
      static_cast<std::uint8_t>(WindowCatalog::kCurrentVersion.value)};
  ae::DomainLoad const load = user.Load(query);
  CHECK(load.result == ae::DomainLoadResult::kEmpty);
}

void ExpectEmbeddedOverlayUserTombstone(std::filesystem::path const& user_dir,
                                        ae::ObjId object_id,
                                        std::uint32_t class_id,
                                        char const* probe_name) {
  ae::RamDomainStorage user_only;
  auto const user_only_profile = user_dir;
  CHECK(apptraverse::LoadUserStorageFile(user_only, user_only_profile));
  ae::DomainQuery const query{object_id, class_id, 0};
  ae::DomainLoad const user_load = user_only.Load(query);
  CHECK(user_load.result == ae::DomainLoadResult::kRemoved);

  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(backing, WindowCatalogEmbeddedGraph());
  ae::RamDomainStorage user;
  auto const user_profile = user_dir;
  CHECK(apptraverse::LoadUserStorageFile(user, user_profile));
  OverlayDomainStorage overlay{
      user, backing, EmbeddedSupplyConstantIdSet(WindowCatalogEmbeddedGraph())};
  ae::DomainLoad const overlay_load = overlay.Load(query);
  CHECK(overlay_load.result == ae::DomainLoadResult::kRemoved);

  std::set<ae::ObjId> const supply =
      EmbeddedSupplyConstantIdSet(WindowCatalogEmbeddedGraph());
  CHECK(supply.count(object_id) == 0);

  std::cerr << "overlay_probe " << probe_name << " obj=" << object_id.id()
            << " user_load=kRemoved overlay_load=kRemoved supply_constant=0"
            << '\n';
}

void WriteProfileMissingPresenterObject(
    std::filesystem::path const& state_dir) {
  ae::RamDomainStorage storage;
  auto const storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile));
  storage.Remove(CatalogMainWindowPresenterObjId());
  CHECK(apptraverse::SaveUserStorageFile(storage, storage_profile));
}

void WriteProfileMissingListObject(std::filesystem::path const& state_dir) {
  ae::RamDomainStorage storage;
  auto const storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile));
  storage.Remove(CatalogListObjId());
  CHECK(apptraverse::SaveUserStorageFile(storage, storage_profile));
}

void AssertListObjectAbsent(std::filesystem::path const& state_dir) {
  ae::RamDomainStorage storage;
  auto const storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile));
  CHECK(storage.Enumerate(CatalogListObjId()).empty());
}

ae::ObjId LoadCatalogLocaleObjId(std::filesystem::path const& state_dir) {
  ProfileGraphStorage storage{state_dir};
  ae::Domain domain{storage.overlay};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->persisted.locale.Load();
  CHECK(catalog->persisted.locale.is_valid());
  return catalog->persisted.locale.id();
}

ae::ObjId WriteProfileMissingLocaleObject(
    std::filesystem::path const& state_dir) {
  ae::ObjId const locale_id = LoadCatalogLocaleObjId(state_dir);
  ae::RamDomainStorage storage;
  auto const storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile));
  storage.Remove(locale_id);
  CHECK(apptraverse::SaveUserStorageFile(storage, storage_profile));
  return locale_id;
}

void AssertLocaleObjectAbsent(std::filesystem::path const& state_dir,
                              ae::ObjId locale_id) {
  ae::RamDomainStorage storage;
  auto const storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile));
  CHECK(storage.Enumerate(locale_id).empty());
}

ae::ObjId LoadAddButtonPresenterObjId(std::filesystem::path const& state_dir) {
  ProfileGraphStorage storage{state_dir};
  ae::Domain domain{storage.overlay};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->persisted.presenters.add_button_presenter.Load();
  CHECK(catalog->persisted.presenters.add_button_presenter.is_valid());
  return catalog->persisted.presenters.add_button_presenter.id();
}

ae::ObjId WriteProfileMissingAddButtonPresenterObject(
    std::filesystem::path const& state_dir) {
  ae::ObjId const presenter_id = LoadAddButtonPresenterObjId(state_dir);
  ae::RamDomainStorage storage;
  auto const storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile));
  storage.Remove(presenter_id);
  CHECK(apptraverse::SaveUserStorageFile(storage, storage_profile));
  return presenter_id;
}

void AssertPresenterObjectAbsent(std::filesystem::path const& state_dir,
                                 ae::ObjId presenter_id) {
  ae::RamDomainStorage storage;
  auto const storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile));
  CHECK(storage.Enumerate(presenter_id).empty());
}

ae::ObjId LoadAddButtonObjId(std::filesystem::path const& state_dir) {
  ProfileGraphStorage storage{state_dir};
  ae::Domain domain{storage.overlay};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->persisted.buttons.add_button.Load();
  CHECK(catalog->persisted.buttons.add_button.is_valid());
  return catalog->persisted.buttons.add_button.id();
}

ae::ObjId WriteProfileMissingAddButtonObject(
    std::filesystem::path const& state_dir) {
  ae::ObjId const button_id = LoadAddButtonObjId(state_dir);
  ae::RamDomainStorage storage;
  auto const storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile));
  storage.Remove(button_id);
  CHECK(apptraverse::SaveUserStorageFile(storage, storage_profile));
  return button_id;
}

void RunRequiredChromeButtonNegativeTests(
    std::filesystem::path const& good_state,
    std::filesystem::path const& load_only) {
  auto const dir = NegativeStateDir("missing_chrome_add_button_object");
  CopyStateDir(good_state, dir);
  ae::ObjId const button_id = WriteProfileMissingAddButtonObject(dir);
  AssertPresenterObjectAbsent(dir, button_id);
  ExpectEmbeddedOverlayUserTombstone(
      dir, button_id, example::surfaces::CatalogAddButton::kClassId,
      "missing_chrome_add_button_object");
  auto const result = RunLoadOnlyCaptureStderr(
      load_only, dir, "missing_chrome_add_button_object");
  ExpectFatalLoadOnly(result, "fatal: LoadApplication failed");
}

ae::ObjId LoadLabelAddObjId(std::filesystem::path const& state_dir) {
  ProfileGraphStorage storage{state_dir};
  ae::Domain domain{storage.overlay};
  WindowCatalog::ptr loaded =
      LoadApplication<WindowCatalog>(domain, CatalogRootObjId());
  ae::Ptr<WindowCatalog> catalog = loaded.Load();
  catalog->persisted.texts.label_add.Load();
  CHECK(catalog->persisted.texts.label_add.is_valid());
  return catalog->persisted.texts.label_add.id();
}

void RunLocalizedTextSupplyConstantTests(
    std::filesystem::path const& good_state) {
  ae::ObjId const text_id = LoadLabelAddObjId(good_state);
  std::set<ae::ObjId> const supply =
      EmbeddedSupplyConstantIdSet(WindowCatalogEmbeddedGraph());
  CHECK(supply.count(text_id) == 1);
  AssertPresenterObjectAbsent(good_state, text_id);
}

void RunRequiredChromePresenterNegativeTests(
    std::filesystem::path const& good_state,
    std::filesystem::path const& load_only) {
  auto const dir =
      NegativeStateDir("missing_chrome_add_button_presenter_object");
  CopyStateDir(good_state, dir);
  ae::ObjId const presenter_id =
      WriteProfileMissingAddButtonPresenterObject(dir);
  AssertPresenterObjectAbsent(dir, presenter_id);
  ExpectEmbeddedOverlayUserTombstone(
      dir, presenter_id, ui::windows::WinPushButtonPresenter::kClassId,
      "missing_chrome_add_button_presenter_object");
  auto const result = RunLoadOnlyCaptureStderr(
      load_only, dir, "missing_chrome_add_button_presenter_object");
  ExpectFatalLoadOnly(result, "fatal: LoadApplication failed");
}

void RunRequiredListLocaleNegativeTests(
    std::filesystem::path const& good_state,
    std::filesystem::path const& load_only) {
  {
    auto const dir = NegativeStateDir("missing_list_object");
    CopyStateDir(good_state, dir);
    WriteProfileMissingListObject(dir);
    AssertListObjectAbsent(dir);
    ExpectEmbeddedOverlayUserTombstone(dir, CatalogListObjId(),
                                       CatalogListContainer::kClassId,
                                       "missing_list_object");
    auto const result =
        RunLoadOnlyCaptureStderr(load_only, dir, "missing_list_object");
    ExpectFatalLoadOnly(result, "fatal: LoadApplication failed");
  }

  {
    auto const dir = NegativeStateDir("missing_locale_object");
    CopyStateDir(good_state, dir);
    ae::ObjId const locale_id = WriteProfileMissingLocaleObject(dir);
    AssertLocaleObjectAbsent(dir, locale_id);
    ExpectEmbeddedOverlayUserTombstone(dir, locale_id,
                                       ui::UiLocaleSettings::kClassId,
                                       "missing_locale_object");
    auto const result =
        RunLoadOnlyCaptureStderr(load_only, dir, "missing_locale_object");
    ExpectFatalLoadOnly(result, "fatal: LoadApplication failed");
  }
}

void RunRequiredMainWindowNegativeTests(
    std::filesystem::path const& good_state,
    std::filesystem::path const& load_only) {
  {
    auto const empty_dir = NegativeStateDir("missing_root_user_only");
    ExpectUserOnlyMissingRootLayer(empty_dir);
  }

  {
    auto const dir = NegativeStateDir("missing_presenter_object");
    CopyStateDir(good_state, dir);
    WriteProfileMissingPresenterObject(dir);
    ExpectEmbeddedOverlayUserTombstone(
        dir, CatalogMainWindowPresenterObjId(),
        ui::windows::WinDesktopWindowPresenter::kClassId,
        "missing_presenter_object");
    auto const result =
        RunLoadOnlyCaptureStderr(load_only, dir, "missing_presenter_object");
    ExpectFatalLoadOnly(result, "fatal: LoadApplication failed");
  }
}

bool CatalogChildHasForeground(HWND hwnd) {
  return hwnd != nullptr && IsWindow(hwnd) && GetForegroundWindow() == hwnd;
}

void OpenExistingCatalogWindowAndWaitForFocus(HWND main, HWND child) {
  // Open is asynchronous. Observing the child's existing focus would not
  // acknowledge this command, which could then restore a later minimization.
  ActivateTestWindow(main);
  ClickControl(main, kOpenControlId);
  CHECK(WaitUntil(std::chrono::seconds{10},
                  [&] { return CatalogChildHasForeground(child); }));
}

std::string Utf8FromWide(std::wstring const& text) {
  if (text.empty()) {
    return {};
  }
  int const size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

char const* ClassifyTopLevelIdentity(HWND expected, HWND found,
                                     std::wstring const& title) {
  if (!IsWindow(expected)) {
    return "original_destroyed";
  }
  if (!IsWindowVisible(expected)) {
    return "original_hidden";
  }
  if (GetWindowTextHwnd(expected).find(title) == std::wstring::npos) {
    return "original_retitled";
  }
  if (found != nullptr) {
    return "other_hwnd_found";
  }
  return "original_not_enumerated";
}

void ExpectSameTopLevelWithTitle(DWORD pid, HWND expected,
                                 std::wstring const& title, char const* step) {
  HWND const found = FindTopLevelWithTitle(pid, title);
  if (found == expected) {
    return;
  }
  std::cerr << "top_level_identity step=" << step << " title=\""
            << Utf8FromWide(title)
            << "\" outcome=" << ClassifyTopLevelIdentity(expected, found, title)
            << " expected=" << expected
            << " expected_is_window=" << (IsWindow(expected) != FALSE)
            << " expected_visible=" << (IsWindowVisible(expected) != FALSE)
            << " expected_text=\"" << Utf8FromWide(GetWindowTextHwnd(expected))
            << "\" found=" << found << " found_text=\""
            << (found != nullptr ? Utf8FromWide(GetWindowTextHwnd(found))
                                 : std::string{})
            << "\"\n";
  for (HWND hwnd : TopLevelWindowsForPid(pid)) {
    std::cerr << "  top_level hwnd=" << hwnd << " text=\""
              << Utf8FromWide(GetWindowTextHwnd(hwnd)) << "\"\n";
  }
  CHECK(found == expected);
}

std::filesystem::path RunRestartPersistenceAcceptance() {
  auto const state_dir = FreshStateDir("window_catalog_win32_acceptance");
  auto const demo = RuntimeDemoExe();
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
  CHECK(WaitUntil(std::chrono::seconds{10},
                  [&] { return TopLevelWindowsForPid(pid).size() == 3; }));

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
  ExpectListRows(main, {L"Window 1 (Closed)", L"Window 2"});
  ExpectSameTopLevelWithTitle(pid, child2, L"Window 2", "after_close_window1");

  SelectListIndex(main, 0);
  CHECK(WaitUntil(std::chrono::seconds{5},
                  [&] { return ControlEnabled(main, kOpenControlId); }));
  ClickControl(main, kOpenControlId);
  HWND child1_reopened = nullptr;
  CHECK(WaitUntil(std::chrono::seconds{15}, [&] {
    child1_reopened = FindTopLevelWithTitle(pid, L"Window 1");
    return child1_reopened != nullptr;
  }));
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return FramesNearEqual(ReadCatalogFrame(child1_reopened), saved_frame);
  }));
  CHECK(WaitUntil(std::chrono::seconds{10},
                  [&] { return CatalogChildHasForeground(child1_reopened); }));
  OpenExistingCatalogWindowAndWaitForFocus(main, child1_reopened);
  ExpectSameTopLevelWithTitle(pid, child1_reopened, L"Window 1",
                              "after_second_open_window1");
  CHECK(FramesNearEqual(ReadCatalogFrame(child1_reopened), saved_frame));

  SendMessageW(child1_reopened, WM_CLOSE, 0, 0);
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return FindTopLevelWithTitle(pid, L"Window 1") == nullptr;
  }));
  ExpectSameTopLevelWithTitle(pid, child2, L"Window 2",
                              "after_close_reopened_window1");

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
    ExpectListRows(main, {L"Window 1 (Closed)", L"Window 2"});
    CHECK(FindTopLevelWithTitle(load_pid, L"Window 1") == nullptr);
    CHECK(FindTopLevelWithTitle(load_pid, L"Window 2") != nullptr);
    CHECK(TopLevelWindowsForPid(load_pid).size() == 2);

    SelectListIndex(main, 0);
    CHECK(WaitUntil(std::chrono::seconds{5},
                    [&] { return ControlEnabled(main, kOpenControlId); }));
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

  auto const demo = RuntimeDemoExe();
  CHECK(std::filesystem::exists(demo));

  PROCESS_INFORMATION pi = StartDemo(demo, state_dir);
  HWND main = WaitCatalogMainWindow(pi.dwProcessId, std::chrono::seconds{30});
  if (main == nullptr) {
    DWORD exit_code = STILL_ACTIVE;
    if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
      GetExitCodeProcess(pi.hProcess, &exit_code);
    }
    std::cerr << "catalog_main_wait_failed pid=" << pi.dwProcessId
              << " process_exited=" << (exit_code != STILL_ACTIVE)
              << " exit_code=" << exit_code << " top_level_visible="
              << TopLevelWindowsForPid(pi.dwProcessId).size() << '\n';
  }
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

  HWND const child2_before = FindTopLevelWithTitle(pi.dwProcessId, L"Window 2");
  CHECK(child2_before != nullptr);
  SendMessageW(child2_before, WM_CLOSE, 0, 0);
  CHECK(WaitUntil(std::chrono::seconds{10},
                  [&] { return !IsWindow(child2_before); }));
  SelectListIndex(main, 1);
  ClickControl(main, kOpenControlId);
  HWND child2_reopened = nullptr;
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    child2_reopened = FindTopLevelWithTitle(pi.dwProcessId, L"Window 2");
    return child2_reopened != nullptr;
  }));
  CHECK(child2_reopened != child2_before);
  CHECK(WaitUntil(std::chrono::seconds{10},
                  [&] { return CatalogChildHasForeground(child2_reopened); }));

  OpenExistingCatalogWindowAndWaitForFocus(main, child2_reopened);

  ShowWindow(child2_reopened, SW_MINIMIZE);
  {
    bool const iconic = WaitUntil(std::chrono::seconds{5},
                                  [&] { return IsIconic(child2_reopened); });
    if (!iconic) {
      WINDOWPLACEMENT placement{};
      placement.length = sizeof(placement);
      if (IsWindow(child2_reopened)) {
        GetWindowPlacement(child2_reopened, &placement);
      }
      std::cerr << "iconic_wait_failed pid=" << pi.dwProcessId
                << " hwnd=" << static_cast<void*>(child2_reopened)
                << " IsWindow=" << IsWindow(child2_reopened)
                << " IsIconic=" << IsIconic(child2_reopened)
                << " IsVisible=" << IsWindowVisible(child2_reopened)
                << " showCmd=" << placement.showCmd << '\n';
    }
    CHECK(iconic);
  }
  SelectListIndex(main, 1);
  ClickControl(main, kOpenControlId);
  CHECK(WaitUntil(std::chrono::seconds{5},
                  [&] { return !IsIconic(child2_reopened); }));

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
                                    ae::ObjId obj_id, std::uint32_t class_id) {
  ae::DomainQuery const query_v0{obj_id, class_id, 0};
  ae::DomainQuery const query_v1{obj_id, class_id, 1};
  ae::RamDomainStorage storage;
  auto const storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile));
  auto& versions = storage.state.at(obj_id)->at(class_id);
  CHECK(versions.count(0) == 1);
  versions[1] = std::move(versions.at(0));
  versions.erase(0);
  CHECK(apptraverse::SaveUserStorageFile(storage, storage_profile));
  CHECK(storage.Load(query_v0).result == ae::DomainLoadResult::kEmpty);
  CHECK(storage.Load(query_v1).result == ae::DomainLoadResult::kLoaded);
}

std::filesystem::path SelfTestExe() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return std::filesystem::path{path};
}

void DiagnoseUnsupportedSchemaOverlayFallback(
    std::filesystem::path const& user_dir) {
  ae::DomainQuery const v0{CatalogRootObjId(), WindowCatalog::kClassId, 0};
  ae::RamDomainStorage user;
  auto const user_profile = user_dir;
  CHECK(apptraverse::LoadUserStorageFile(user, user_profile));
  CHECK(user.Load(v0).result == ae::DomainLoadResult::kEmpty);
  ae::DomainQuery const v1{CatalogRootObjId(), WindowCatalog::kClassId, 1};
  CHECK(user.Load(v1).result == ae::DomainLoadResult::kLoaded);

  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(backing, WindowCatalogEmbeddedGraph());
  OverlayDomainStorage overlay{
      user, backing, EmbeddedSupplyConstantIdSet(WindowCatalogEmbeddedGraph())};
  ae::DomainLoad const overlay_v0 = overlay.Load(v0);
  std::cerr
      << "unsupported_schema_overlay_probe v0_after_user_v1_only result="
      << static_cast<int>(overlay_v0.result)
      << " (embedded subprocess would not fatal; USER-only probe required)"
      << '\n';
  CHECK(overlay_v0.result == ae::DomainLoadResult::kLoaded);
}

ChildRunResult RunUserOnlyLoadProbeCaptureStderr(
    std::filesystem::path const& state_dir, char const* scenario,
    DWORD timeout_ms = 30000) {
  auto const exe = SelfTestExe();
  auto err_path = state_dir;
  err_path += L".stderr.txt";
  std::error_code ec;
  std::filesystem::remove(err_path, ec);

  std::wstring cmd = L"\"" + exe.wstring() + L"\" --user-only-load-probe \"" +
                     std::wstring(scenario, scenario + std::strlen(scenario)) +
                     L"\" \"" + state_dir.wstring() + L"\"";
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE errf =
      CreateFileW(err_path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                  &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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
  BOOL ok =
      CreateProcessW(exe.wstring().c_str(), cmd_buf.data(), nullptr, nullptr,
                     TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(errf);
  CloseHandle(nul);
  CHECK(ok);
  CloseHandle(pi.hThread);

  DWORD wait = WaitForSingleObject(pi.hProcess, timeout_ms);
  if (wait == WAIT_TIMEOUT) {
    std::cerr << "user_only_load_probe_timeout scenario=" << scenario
              << " exe=" << exe.string() << " state_dir=" << state_dir.string()
              << '\n';
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

void ExpectUnsupportedSchemaLoadOnly(char const* scenario,
                                     ChildRunResult const& result) {
  bool const load_failed =
      result.stderr_text.find("fatal: LoadApplication failed") !=
      std::string::npos;
  if (result.exit_code == 0 || !load_failed) {
    std::cerr << "unsupported-schema scenario: " << scenario << '\n'
              << "child exit: dec=" << result.exit_code << " hex=0x" << std::hex
              << result.exit_code << std::dec << '\n'
              << "load_failed=" << load_failed << '\n'
              << "captured stderr:\n"
              << result.stderr_text << '\n';
  }
  CHECK(result.exit_code != 0);
  CHECK(load_failed);
}

std::uint32_t UnknownStoredClassIdForTests() {
  constexpr std::uint32_t kUnknown = 0xC1D00001u;
  CHECK(kUnknown != ae::Obj::kClassId);
  return kUnknown;
}

bool EnumerateContainsClass(ae::RamDomainStorage& storage, ae::ObjId obj_id,
                            std::uint32_t class_id) {
  for (auto const stored : storage.Enumerate(obj_id)) {
    if (stored == class_id) {
      return true;
    }
  }
  return false;
}

void CopyStoredLayer(std::filesystem::path const& state_dir,
                     ae::DomainQuery const& from, ae::DomainQuery const& to) {
  ae::RamDomainStorage storage;
  auto const storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile));
  auto bytes = storage.state.at(from.id)->at(from.class_id).at(from.version);
  storage.SaveData(to, std::move(bytes));
  CHECK(apptraverse::SaveUserStorageFile(storage, storage_profile));
}

void AddUnknownStoredClassLayerAlongsideRoot(
    std::filesystem::path const& state_dir) {
  std::uint32_t const unknown = UnknownStoredClassIdForTests();
  ae::ObjId const root = CatalogRootObjId();
  ae::DomainQuery const src{root, WindowCatalog::kClassId, 0};
  ae::DomainQuery const dst{root, unknown, 0};
  CopyStoredLayer(state_dir, src, dst);
  ae::RamDomainStorage storage;
  auto const storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile));
  CHECK(EnumerateContainsClass(storage, root, WindowCatalog::kClassId));
  CHECK(EnumerateContainsClass(storage, root, unknown));
}

void RelocateStoredClassDirectory(std::filesystem::path const& state_dir,
                                  ae::ObjId obj_id, std::uint32_t from_class_id,
                                  std::uint32_t to_class_id) {
  ae::RamDomainStorage storage;
  auto const storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile));
  auto& classes = *storage.state.at(obj_id);
  CHECK(classes.count(from_class_id) == 1);
  classes[to_class_id] = std::move(classes.at(from_class_id));
  classes.erase(from_class_id);
  CHECK(apptraverse::SaveUserStorageFile(storage, storage_profile));
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
    auto const result = RunUserOnlyLoadProbeCaptureStderr(
        dir, "unknown_class_id_alongside_root");
    ExpectUnsupportedSchemaLoadOnly("unknown_class_id_alongside_root", result);
  }

  {
    auto const dir = NegativeStateDir("unknown_class_id_main_window_presenter");
    CopyStateDir(good_state, dir);
    RelocateStoredClassDirectory(
        dir, CatalogMainWindowPresenterObjId(),
        ui::windows::WinDesktopWindowPresenter::kClassId,
        UnknownStoredClassIdForTests());
    auto const result = RunUserOnlyLoadProbeCaptureStderr(
        dir, "unknown_class_id_main_window_presenter");
    ExpectUnsupportedSchemaLoadOnly("unknown_class_id_main_window_presenter",
                                    result);
  }
}

void RunRequiredUnsupportedSchemaNegativeTests(
    std::filesystem::path const& good_state,
    std::filesystem::path const& /*load_only*/) {
  {
    auto const dir = NegativeStateDir("unsupported_schema_window_catalog_root");
    CopyStateDir(good_state, dir);
    RelocateStoredLayerVersion0To1(dir, CatalogRootObjId(),
                                   WindowCatalog::kClassId);
    DiagnoseUnsupportedSchemaOverlayFallback(dir);
    auto const result = RunUserOnlyLoadProbeCaptureStderr(
        dir, "unsupported_schema_window_catalog_root");
    ExpectUnsupportedSchemaLoadOnly("unsupported_schema_window_catalog_root",
                                    result);
  }

  {
    auto const dir =
        NegativeStateDir("unsupported_schema_main_window_presenter");
    CopyStateDir(good_state, dir);
    RelocateStoredLayerVersion0To1(
        dir, CatalogMainWindowPresenterObjId(),
        ui::windows::WinDesktopWindowPresenter::kClassId);
    auto const result = RunUserOnlyLoadProbeCaptureStderr(
        dir, "unsupported_schema_main_window_presenter");
    ExpectUnsupportedSchemaLoadOnly("unsupported_schema_main_window_presenter",
                                    result);
  }
}

void RunDeleteAndColdReloadAcceptance() {
  auto const state_dir = FreshStateDir("window_catalog_win32_delete_cold");
  auto const demo = RuntimeDemoExe();
  auto const load_only = DemoExe("win32_window_catalog_demo_load_only.exe");
  CHECK(std::filesystem::exists(demo));
  CHECK(std::filesystem::exists(load_only));

  ActiveDemoProcess session{StartDemo(demo, state_dir)};
  DWORD const pid = session.pi.dwProcessId;
  HWND main = WaitCatalogMainWindow(pid, std::chrono::seconds{30});
  CHECK(main != nullptr);
  WaitChromeReady(main);

  for (int i = 0; i < 2; ++i) {
    ClickControl(main, kAddControlId);
    CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
      HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
      return list && SendMessageW(list, LB_GETCOUNT, 0, 0) == i + 1;
    }));
  }
  ExpectListRows(main, {L"Window 1", L"Window 2"});

  SelectListIndex(main, 0);
  CHECK(WaitUntil(std::chrono::seconds{5},
                  [&] { return ControlEnabled(main, kDeleteControlId); }));
  ClickControl(main, kDeleteControlId);
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
    return list && SendMessageW(list, LB_GETCOUNT, 0, 0) == 1;
  }));
  ExpectListRows(main, {L"Window 2"});

  CloseProcessGracefully(main, session.pi);
  session.Release();

  {
    ActiveDemoProcess reload{StartDemo(load_only, state_dir)};
    DWORD const reload_pid = reload.pi.dwProcessId;
    main = WaitCatalogMainWindow(reload_pid, std::chrono::seconds{30});
    CHECK(main != nullptr);
    WaitChromeReady(main);
    ExpectListRows(main, {L"Window 2"});
    ClickControl(main, kAddControlId);
    CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
      HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
      return list && SendMessageW(list, LB_GETCOUNT, 0, 0) == 2;
    }));
    ExpectListRows(main, {L"Window 2", L"Window 3"});
    CloseProcessGracefully(main, reload.pi);
    reload.Release();
  }

  {
    ActiveDemoProcess restart{StartDemo(demo, state_dir)};
    main =
        WaitCatalogMainWindow(restart.pi.dwProcessId, std::chrono::seconds{30});
    CHECK(main != nullptr);
    WaitChromeReady(main);
    ExpectListRows(main, {L"Window 2", L"Window 3"});
    CloseProcessGracefully(main, restart.pi);
    restart.Release();
  }
}

void RunAcceptanceAndRequiredMainWindowNegatives() {
#if defined(APPTRAVERSE_TEST_DEBUG_BOOTSTRAP)
  // Normal Debug deliberately ignores previous USER, unlike the load consumer.
  auto const debug_state = FreshStateDir("window_catalog_debug_always_fresh");
  for (int run = 0; run != 2; ++run) {
    ActiveDemoProcess debug{
        StartDemo(DemoExe("win32_window_catalog_demo.exe"), debug_state)};
    HWND main =
        WaitCatalogMainWindow(debug.pi.dwProcessId, std::chrono::seconds{30});
    CHECK(main != nullptr);
    WaitChromeReady(main);
    ExpectListRows(main, {});
    CHECK(TopLevelWindowsForPid(debug.pi.dwProcessId).size() == 1);
    ClickControl(main, kAddControlId);
    CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
      return TopLevelWindowsForPid(debug.pi.dwProcessId).size() == 2;
    }));
    ExpectListRows(main, {L"Window 1"});
    CloseProcessGracefully(main, debug.pi);
    debug.Release();
  }
  std::size_t debug_runs = 0;
  for (auto const& entry : std::filesystem::directory_iterator(debug_state)) {
    if (entry.is_directory() &&
        entry.path().filename().string().starts_with("debug-run-")) {
      CHECK(std::filesystem::exists(entry.path() / "user"));
      CHECK(std::filesystem::exists(
          entry.path() / "embedded_distilled_window_catalog_supply.h"));
      ++debug_runs;
    }
  }
  CHECK(debug_runs == 2);
#endif
  auto const good_state = RunRestartPersistenceAcceptance();
  RunDeleteAndColdReloadAcceptance();
  RunLanguageAndFocusAcceptance();
  auto const load_only = DemoExe("win32_window_catalog_demo_load_only.exe");
  RunRequiredMainWindowNegativeTests(good_state, load_only);
  RunRequiredListLocaleNegativeTests(good_state, load_only);
  RunRequiredChromePresenterNegativeTests(good_state, load_only);
  RunRequiredChromeButtonNegativeTests(good_state, load_only);
  RunLocalizedTextSupplyConstantTests(good_state);
  RunRequiredUnsupportedSchemaNegativeTests(good_state, load_only);
  RunRequiredUnknownClassIdNegativeTests(good_state, load_only);
}

}  // namespace
}  // namespace apptraverse::test

int main(int argc, char** argv) {
  apptraverse::EnsureWindowCatalogGraphRegistration();
  if (argc >= 4 && std::strcmp(argv[1], "--user-only-load-probe") == 0) {
    ae::RamDomainStorage user;
    auto const user_profile = argv[3];
    if (!apptraverse::LoadUserStorageFile(user, user_profile)) return 2;
    ae::RamDomainStorage supply;
    apptraverse::test::InstallEmbeddedSupplyConstantLayers(supply);
    apptraverse::OverlayDomainStorage storage{
        user, supply,
        apptraverse::EmbeddedSupplyConstantIdSet(
            apptraverse::test::WindowCatalogEmbeddedGraph())};
    ae::Domain domain{storage};
    (void)apptraverse::LoadApplication<apptraverse::WindowCatalog>(
        domain, apptraverse::test::CatalogRootObjId());
    return 1;
  }
  apptraverse::test::RunAcceptanceAndRequiredMainWindowNegatives();
  std::cout << "window_catalog_win32_acceptance_test OK\n";
  return 0;
}
