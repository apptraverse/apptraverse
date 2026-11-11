#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/ui/ui_presenters.h"

#include "minimal_app_controls.h"
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"
#include "minimal_application.h"
#include "minimal_app_registration.h"

#if defined(APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL)
#include "embedded_distilled_minimal_app_supply.h"
#endif

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

constexpr wchar_t kDesktopClass[] = L"AppTraverseUiDesktopWindow";
constexpr wchar_t kLoadingClass[] = L"AppTraverseMinimalAppLoading";

struct Frame {
  int x = 0;
  int y = 0;
  int client_width = 0;
  int client_height = 0;
};

Frame ReadFrame(HWND hwnd) {
  Frame frame;
  apptraverse::ui::windows::ReadModelFrameFromHwnd(
      hwnd, frame.x, frame.y, frame.client_width, frame.client_height);
  return frame;
}

bool FramesNear(Frame const& a, Frame const& b, int tolerance = 3) {
  return std::abs(a.x - b.x) <= tolerance &&
         std::abs(a.y - b.y) <= tolerance &&
         std::abs(a.client_width - b.client_width) <= tolerance &&
         std::abs(a.client_height - b.client_height) <= tolerance;
}

void ApplyFrame(HWND hwnd, int x, int y, int cw, int ch) {
  int outer_w = 0;
  int outer_h = 0;
  apptraverse::ui::windows::ClientSizeToOuter(cw, ch, outer_w, outer_h);
  CHECK(SetWindowPos(hwnd, nullptr, x, y, outer_w, outer_h,
                     SWP_NOZORDER | SWP_NOACTIVATE) != 0);
  SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
}

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

std::filesystem::path ExeDir() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return std::filesystem::path{path}.parent_path();
}

std::filesystem::path DemoExe(char const* name) {
  std::error_code ec;
  auto try_path = [&](std::filesystem::path const& p) {
    auto abs = std::filesystem::weakly_canonical(p, ec);
    if (!ec && std::filesystem::exists(abs)) {
      return abs;
    }
    return std::filesystem::path{};
  };
  if (auto p = try_path(ExeDir() / name); !p.empty()) {
    return p;
  }
  if (auto p = try_path(ExeDir() / ".." / "examples" / "minimal_app" / "windows" /
                        name);
      !p.empty()) {
    return p;
  }
  return ExeDir() / name;
}

std::filesystem::path RuntimeDemoExe() {
#if defined(APPTRAVERSE_TEST_DEBUG_BOOTSTRAP)
  return DemoExe("win32_minimal_app_load_only.exe");
#else
  return DemoExe("win32_minimal_app.exe");
#endif
}

std::filesystem::path FreshStateDir() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_test" / "minimal_app_win32_acceptance";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::filesystem::path NegativeStateDir(char const* suffix) {
  auto dir = std::filesystem::temp_directory_path() / "apptraverse_test" /
             ("minimal_app_win32_acceptance_" + std::string{suffix});
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

struct ProfileDomain {
#if defined(APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL)
  ae::RamDomainStorage backing;
  std::filesystem::path user_storage_profile;
  ae::RamDomainStorage user_storage;
  OverlayDomainStorage overlay;
  ae::Domain domain;

  explicit ProfileDomain(std::filesystem::path const& state_dir)
      : user_storage_profile{state_dir},
        overlay{user_storage,
                backing,
                EmbeddedSupplyConstantIdSet(
                    embedded_distilled::minimal::kEmbeddedGraph)},
        domain{overlay} {
    CHECK(apptraverse::LoadUserStorageFile(user_storage, user_storage_profile));
    InstallEmbeddedDistilledBacking(
        backing, embedded_distilled::minimal::kEmbeddedGraph);
  }
#else
  std::filesystem::path storage_profile;
  ae::RamDomainStorage storage;
  ae::Domain domain;

  explicit ProfileDomain(std::filesystem::path const& state_dir)
      : storage_profile{state_dir}, domain{storage} { CHECK(apptraverse::LoadUserStorageFile(storage, storage_profile)); }
#endif
  bool SaveFile() {
#if defined(APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL)
    return apptraverse::SaveUserStorageFile(user_storage, user_storage_profile);
#else
    return apptraverse::SaveUserStorageFile(storage, storage_profile);
#endif
  }

};

void WriteProfileMissingMainWindow(std::filesystem::path const& state_dir) {
  ProfileDomain profile{state_dir};
  ae::Domain& domain = profile.domain;
  example::minimal::MinimalApplication::ptr loaded =
      LoadApplication<example::minimal::MinimalApplication>(
          domain, example::minimal::MinimalApplicationRootId());
  ae::Ptr<example::minimal::MinimalApplication> app = loaded.Load();
  app->main_window = ui::UiDesktopWindow::ptr::Declare(
      ae::CreateWith{domain}.with_id(ae::ObjId{}));
  if (app->base.is_valid()) {
    if (!app->base.is_loaded()) {
      app->base.Load();
    }
    auto& base_app = static_cast<example::minimal::MinimalApplication&>(
        *app->base.operator->());
    base_app.main_window = ui::UiDesktopWindow::ptr::Declare(
        ae::CreateWith{domain}.with_id(ae::ObjId{}));
    app->base.Save();
  }
  ae::DomainGraph graph{&domain};
  CHECK(graph.SaveRoot(app, example::minimal::MinimalApplicationRootId())
            .IsOk());
  CHECK(profile.SaveFile());
}

void AssertMainWindowRefAbsent(std::filesystem::path const& state_dir) {
  ProfileDomain profile{state_dir};
  ae::Domain& domain = profile.domain;
  example::minimal::MinimalApplication::ptr loaded =
      LoadApplication<example::minimal::MinimalApplication>(
          domain, example::minimal::MinimalApplicationRootId());
  ae::Ptr<example::minimal::MinimalApplication> app = loaded.Load();
  CHECK(!app->main_window.is_valid());
}

void WriteProfileMissingPresenter(std::filesystem::path const& state_dir) {
  ProfileDomain profile{state_dir};
  ae::Domain& domain = profile.domain;
  example::minimal::MinimalApplication::ptr loaded =
      LoadApplication<example::minimal::MinimalApplication>(
          domain, example::minimal::MinimalApplicationRootId());
  ae::Ptr<example::minimal::MinimalApplication> app = loaded.Load();
  ae::Ptr<ui::UiDesktopWindow> window = app->main_window.Load();
  window->presenter = ui::windows::WinDesktopWindowPresenter::ptr::Declare(
      ae::CreateWith{domain}.with_id(ae::ObjId{}));
  ae::DomainGraph graph{&domain};
  CHECK(graph.SaveRoot(window, example::minimal::MinimalMainWindowId()).IsOk());
  CHECK(profile.SaveFile());
}

void WriteProfileMissingPresenterObject(std::filesystem::path const& state_dir) {
  ae::RamDomainStorage user_storage; auto const user_storage_profile = state_dir;
  CHECK(apptraverse::LoadUserStorageFile(user_storage, user_storage_profile));
  user_storage.Remove(example::minimal::MinimalPresenterId());
  CHECK(apptraverse::SaveUserStorageFile(user_storage, user_storage_profile));
}

PROCESS_INFORMATION StartDemo(std::filesystem::path const& exe,
                              std::filesystem::path const& state_dir) {
  std::wstring cmd = L"\"" + exe.wstring() + L"\" --state-dir \"" +
                     state_dir.wstring() + L"\"";
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  CHECK(CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       nullptr, &si, &pi));
  CloseHandle(pi.hThread);
  return pi;
}

std::uint32_t HwndControlId(HWND hwnd) {
  return static_cast<std::uint32_t>(GetDlgCtrlID(hwnd));
}

HWND FindMainChildControl(HWND main, std::uint32_t control_id) {
  HWND found = nullptr;
  struct Ctx {
    HWND main;
    std::uint32_t control_id;
    HWND* out;
  } ctx{main, control_id, &found};
  EnumChildWindows(
      main,
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        if (GetParent(hwnd) != c->main) {
          return TRUE;
        }
        if (HwndControlId(hwnd) == c->control_id) {
          *c->out = hwnd;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return found;
}

std::wstring ReadWindowTitle(HWND hwnd) {
  int const len = GetWindowTextLengthW(hwnd);
  if (len <= 0) {
    return {};
  }
  std::wstring title(static_cast<std::size_t>(len), L'\0');
  GetWindowTextW(hwnd, title.data(), len + 1);
  title.resize(static_cast<std::size_t>(len));
  return title;
}

void SelectListRow(HWND main, int index) {
  HWND const list =
      FindMainChildControl(main, example::minimal::kMinimalRowListControlId);
  CHECK(list != nullptr);
  SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
  SendMessageW(main, WM_COMMAND,
               MAKEWPARAM(example::minimal::kMinimalRowListControlId,
                          LBN_SELCHANGE),
               reinterpret_cast<LPARAM>(list));
}

int ListSelection(HWND main) {
  HWND const list =
      FindMainChildControl(main, example::minimal::kMinimalRowListControlId);
  if (list == nullptr) {
    return LB_ERR;
  }
  return static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
}

void ClickApplyButton(HWND main) {
  HWND const button =
      FindMainChildControl(main, example::minimal::kMinimalApplyButtonControlId);
  CHECK(button != nullptr);
  SendMessageW(main, WM_COMMAND,
               MAKEWPARAM(example::minimal::kMinimalApplyButtonControlId,
                          BN_CLICKED),
               reinterpret_cast<LPARAM>(button));
}

bool MainWindowShowsAppliedState(HWND main) {
  std::wstring const title = ReadWindowTitle(main);
  if (title.find(L"Applied") == std::wstring::npos) {
    return false;
  }
  HWND const button =
      FindMainChildControl(main, example::minimal::kMinimalApplyButtonControlId);
  if (button == nullptr || IsWindowEnabled(button)) {
    return false;
  }
  int const sel = ListSelection(main);
  return sel == LB_ERR || sel < 0;
}

void WaitMainWindowAppliedState(HWND main) {
  CHECK(WaitUntil(std::chrono::seconds{30}, [&] {
    return MainWindowShowsAppliedState(main);
  }));
}

struct MainWindowControlCounts {
  int apply = 0;
  int row_list = 0;
  int auxiliary_opener = 0;
};

void LogChildWindowDiagnostics(HWND parent, char const* label) {
  std::cerr << "Win32 control diagnostics (" << label << ") parent=" << parent
            << '\n';
  EnumChildWindows(
      parent,
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        HWND const expected_parent = reinterpret_cast<HWND>(lparam);
        wchar_t cls[128]{};
        GetClassNameW(hwnd, cls, 128);
        std::cerr << "  hwnd=" << hwnd << " parent=" << GetParent(hwnd)
                  << " control_id=" << HwndControlId(hwnd) << " class=\"";
        std::wcerr << cls;
        std::cerr << '"';
        if (GetParent(hwnd) != expected_parent) {
          std::cerr << " (unexpected parent)";
        }
        std::cerr << '\n';
        return TRUE;
      },
      reinterpret_cast<LPARAM>(parent));
}

MainWindowControlCounts CountMainWindowNamedControls(HWND main) {
  MainWindowControlCounts counts{};
  struct Ctx {
    HWND main;
    MainWindowControlCounts* counts;
  } ctx{main, &counts};
  EnumChildWindows(
      main,
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* ctx = reinterpret_cast<Ctx*>(lparam);
        if (GetParent(hwnd) != ctx->main) {
          return TRUE;
        }
        switch (HwndControlId(hwnd)) {
          case example::minimal::kMinimalApplyButtonControlId:
            ++ctx->counts->apply;
            break;
          case example::minimal::kMinimalRowListControlId:
            ++ctx->counts->row_list;
            break;
          case example::minimal::kMinimalAuxiliaryOpenerControlId:
            ++ctx->counts->auxiliary_opener;
            break;
          default:
            break;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return counts;
}

bool MainWindowNamedControlsReady(HWND main) {
  auto const counts = CountMainWindowNamedControls(main);
  return counts.apply == 1 && counts.row_list == 1 &&
         counts.auxiliary_opener == 1;
}

void CheckMainWindowNamedControls(HWND main) {
  auto const counts = CountMainWindowNamedControls(main);
  if (counts.apply != 1 || counts.row_list != 1 ||
      counts.auxiliary_opener != 1) {
    LogChildWindowDiagnostics(main, "main window named controls");
    CHECK(false);
  }
}

bool WaitForMainWindowNamedControls(HWND main,
                                    std::chrono::milliseconds timeout) {
  if (WaitUntil(timeout, [&] { return MainWindowNamedControlsReady(main); })) {
    return true;
  }
  LogChildWindowDiagnostics(main, "WaitForMainWindowNamedControls timeout");
  return false;
}

std::vector<HWND> DesktopWindowsForPid(DWORD pid) {
  std::vector<HWND> out;
  struct Ctx {
    DWORD pid;
    std::vector<HWND>* out;
  } ctx{pid, &out};
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
        if (wcscmp(cls, kLoadingClass) == 0) {
          return TRUE;
        }
        if (wcscmp(cls, kDesktopClass) != 0) {
          return TRUE;
        }
        c->out->push_back(hwnd);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return out;
}

HWND WaitMainWindow(DWORD pid, std::chrono::milliseconds timeout) {
  HWND found = nullptr;
  WaitUntil(timeout, [&] {
    auto windows = DesktopWindowsForPid(pid);
    if (windows.size() == 1) {
      found = windows[0];
      return true;
    }
    return false;
  });
  return found;
}

void CloseGracefully(HWND main, PROCESS_INFORMATION& pi) {
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

void ProbePersistedGraph(std::filesystem::path const& state_dir,
                         Frame const& expected_frame,
                         char const* expected_title_utf8 = "Minimal App") {
  ProfileDomain profile{state_dir};
  auto application = LoadApplication<example::minimal::MinimalApplication>(
      profile.domain, example::minimal::MinimalApplicationRootId());

  CHECK(application->obj_id == example::minimal::MinimalApplicationRootId());
  CHECK(application->main_window.is_valid());
  CHECK(application->main_window.id() ==
        example::minimal::MinimalMainWindowId());
  if (!application->main_window.is_loaded()) {
    application->main_window.Load();
  }
  auto& window =
      static_cast<ui::UiDesktopWindow&>(*application->main_window.operator->());
  CHECK(window.presenter.is_valid());
  CHECK(window.presenter.id() == example::minimal::MinimalPresenterId());
  if (!window.presenter.is_loaded()) {
    window.presenter.Load();
  }
  CHECK(window.presenter->title.is_valid());
  CHECK(window.presenter->locale.is_valid());
  CHECK(window.presenter->title->Resolve(0) == expected_title_utf8);
  CHECK(window.presenter->window.id() == example::minimal::MinimalMainWindowId());

  CHECK(application->base.is_valid());
  CHECK(application->base.is_loaded());
  auto const& base_app =
      static_cast<example::minimal::MinimalApplication const&>(
          *application->base.operator->());
  CHECK(base_app.main_window.is_valid());
  CHECK(base_app.main_window.id() == example::minimal::MinimalMainWindowId());
  auto const& base_window = static_cast<ui::UiDesktopWindow const&>(
      *base_app.main_window.operator->());
  CHECK(base_window.presenter.is_valid());
  CHECK(base_window.presenter.id() == example::minimal::MinimalPresenterId());

  Frame model_frame;
  model_frame.x = window.x();
  model_frame.y = window.y();
  model_frame.client_width = window.client_width();
  model_frame.client_height = window.client_height();
  CHECK(FramesNear(model_frame, expected_frame));
}

#if defined(APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL)
void ProbePersistedEmbeddedUserState(std::filesystem::path const& state_dir,
                                     int expected_selection,
                                     char const* expected_title_utf8) {
  ProfileDomain profile{state_dir};
  example::minimal::MinimalApplication::ptr loaded =
      LoadApplication<example::minimal::MinimalApplication>(
          profile.domain, example::minimal::MinimalApplicationRootId());
  example::minimal::PrepareMinimalApplicationForRun(*loaded);
  loaded->row_list.Load();
  loaded->apply_button.Load();
  loaded->row_list->window_title.Load();
  CHECK(loaded->row_list);
  CHECK(loaded->row_list->window_title);
  CHECK(loaded->row_list->selection_index == expected_selection);
  CHECK(loaded->row_list->window_title->Resolve(0) == expected_title_utf8);
  CHECK(loaded->row_list->base.is_valid());
  CHECK(loaded->row_list->base.is_loaded());
}

void ProbePersistedEmbeddedUserCommitReady(
    std::filesystem::path const& state_dir, int expected_selection,
    char const* expected_title_utf8, int commit_selection) {
  ProfileDomain profile{state_dir};
  example::minimal::MinimalApplication::ptr loaded =
      LoadApplication<example::minimal::MinimalApplication>(
          profile.domain, example::minimal::MinimalApplicationRootId());
  example::minimal::PrepareMinimalApplicationForRun(*loaded);
  loaded->row_list.Load();
  loaded->row_list->window_title.Load();
  CHECK(loaded->row_list->selection_index == expected_selection);
  CHECK(loaded->row_list->window_title->Resolve(0) == expected_title_utf8);
  CHECK(loaded->row_list->base.is_valid());
  example::minimal::MinimalAppList& list =
      static_cast<example::minimal::MinimalAppList&>(*loaded->row_list);
  list.HandleListRowSelectionChanged(static_cast<std::size_t>(commit_selection));
  CHECK(list.selection_index == commit_selection);
}

void ExpectEmbeddedBackingUnchanged() {
  ae::RamDomainStorage reference;
  InstallEmbeddedDistilledBacking(
      reference, embedded_distilled::minimal::kEmbeddedGraph);
  ae::RamDomainStorage::ObjClassData const snapshot = reference.state;
  ae::RamDomainStorage probe;
  InstallEmbeddedDistilledBacking(
      probe, embedded_distilled::minimal::kEmbeddedGraph);
  CHECK(probe.state == snapshot);
}
#endif

std::filesystem::path RunAcceptanceEmbeddedTwoCycle() {
  auto const state_dir = FreshStateDir();
  auto const demo = RuntimeDemoExe();
  auto const load_only = DemoExe("win32_minimal_app_load_only.exe");
  CHECK(std::filesystem::is_empty(state_dir));
  ExpectEmbeddedBackingUnchanged();

  // Cycle 1: empty USER — frame UI command (resize), shutdown persist.
  PROCESS_INFORMATION pi = StartDemo(demo, state_dir);
  CHECK(WaitUntil(std::chrono::seconds{5}, [&] {
    return FindWindowW(kLoadingClass, nullptr) == nullptr;
  }));
  HWND main = WaitMainWindow(pi.dwProcessId, std::chrono::seconds{30});
  CHECK(main != nullptr);
  CHECK(WaitForMainWindowNamedControls(main, std::chrono::seconds{15}));
  Frame const cycle1_frame{220, 140, 720, 460};
  ApplyFrame(main, cycle1_frame.x, cycle1_frame.y, cycle1_frame.client_width,
             cycle1_frame.client_height);
  CHECK(WaitUntil(std::chrono::seconds{15}, [&] {
    return FramesNear(ReadFrame(main), cycle1_frame);
  }));
  CloseGracefully(main, pi);
  CHECK(!std::filesystem::is_empty(state_dir));
  ExpectEmbeddedBackingUnchanged();
  ProbePersistedGraph(state_dir, cycle1_frame);

  // Cycle 2: USER profile — list row B + Apply (journal-bearing UI commands).
  pi = StartDemo(demo, state_dir);
  main = WaitMainWindow(pi.dwProcessId, std::chrono::seconds{30});
  CHECK(main != nullptr);
  CHECK(WaitForMainWindowNamedControls(main, std::chrono::seconds{15}));
  CHECK(FramesNear(ReadFrame(main), cycle1_frame));
  SelectListRow(main, 1);
  CHECK(WaitUntil(std::chrono::seconds{15}, [&] {
    return ListSelection(main) == 1;
  }));
  ClickApplyButton(main);
  WaitMainWindowAppliedState(main);
  CloseGracefully(main, pi);
  ExpectEmbeddedBackingUnchanged();
  ProbePersistedGraph(state_dir, cycle1_frame, "Applied");
  ProbePersistedEmbeddedUserState(state_dir, -1, "Applied");

  // Cold load 3: Applied mirror; new list UI command (row A).
  pi = StartDemo(load_only, state_dir);
  main = WaitMainWindow(pi.dwProcessId, std::chrono::seconds{30});
  CHECK(main != nullptr);
  CHECK(WaitForMainWindowNamedControls(main, std::chrono::seconds{15}));
  CHECK(FramesNear(ReadFrame(main), cycle1_frame));
  CHECK(MainWindowShowsAppliedState(main));
  SelectListRow(main, 0);
  CHECK(WaitUntil(std::chrono::seconds{15}, [&] {
    return ListSelection(main) == 0;
  }));
  CloseGracefully(main, pi);
  ExpectEmbeddedBackingUnchanged();
  ProbePersistedEmbeddedUserCommitReady(state_dir, 0, "Applied", 1);
  return state_dir;
}

std::filesystem::path RunAcceptanceFrameColdLoad() {
  auto const state_dir = FreshStateDir();
  auto const demo = RuntimeDemoExe();
  auto const load_only = DemoExe("win32_minimal_app_load_only.exe");
  CHECK(std::filesystem::exists(demo));
  CHECK(std::filesystem::exists(load_only));

  PROCESS_INFORMATION pi = StartDemo(demo, state_dir);
  DWORD const pid = pi.dwProcessId;

  CHECK(WaitUntil(std::chrono::seconds{5}, [&] {
    return FindWindowW(kLoadingClass, nullptr) == nullptr;
  }));

  HWND main = WaitMainWindow(pid, std::chrono::seconds{30});
  CHECK(main != nullptr);
  CHECK(WaitForMainWindowNamedControls(main, std::chrono::seconds{15}));
  CheckMainWindowNamedControls(main);
  CHECK(DesktopWindowsForPid(pid).size() == 1);

  ApplyFrame(main, 220, 140, 720, 460);
  Frame const saved = ReadFrame(main);
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return FramesNear(ReadFrame(main), saved);
  }));

  CloseGracefully(main, pi);
  ProbePersistedGraph(state_dir, saved);

  pi = StartDemo(load_only, state_dir);
  DWORD const load_pid = pi.dwProcessId;
  main = WaitMainWindow(load_pid, std::chrono::seconds{30});
  CHECK(main != nullptr);
  CHECK(WaitForMainWindowNamedControls(main, std::chrono::seconds{15}));
  CheckMainWindowNamedControls(main);
  CHECK(DesktopWindowsForPid(load_pid).size() == 1);
  CHECK(FramesNear(ReadFrame(main), saved));

  CloseGracefully(main, pi);
  return state_dir;
}

std::filesystem::path RunAcceptance() {
  auto const demo = RuntimeDemoExe();
  auto const load_only = DemoExe("win32_minimal_app_load_only.exe");
  CHECK(std::filesystem::exists(demo));
  CHECK(std::filesystem::exists(load_only));
#if defined(APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL)
  return RunAcceptanceEmbeddedTwoCycle();
#else
  return RunAcceptanceFrameColdLoad();
#endif
}

void RunRequiredGraphNegativeTests(std::filesystem::path const& good_state,
                                   std::filesystem::path const& load_only) {
  {
    auto const empty_dir = NegativeStateDir("missing_root");
#if defined(APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL)
    PROCESS_INFORMATION pi = StartDemo(load_only, empty_dir);
    HWND main = WaitMainWindow(pi.dwProcessId, std::chrono::seconds{30});
    CHECK(main != nullptr);
    Frame const frame = ReadFrame(main);
    CHECK(FramesNear(frame, Frame{120, 80, 640, 480}));
    CloseGracefully(main, pi);
#else
    auto const result = RunLoadOnlyCaptureStderr(load_only, empty_dir);
    ExpectFatalLoadOnly(result, "fatal: LoadApplication failed");
#endif
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

#if defined(APPTRAVERSE_TEST_DEBUG_BOOTSTRAP)
void TestDebugAlwaysCreatesFreshGraph(std::filesystem::path const& existing_profile) {
  auto const previous_user = ReadAllBytes(existing_profile / "user.bin");
  for (int run = 0; run < 2; ++run) {
    std::size_t before = 0;
    for (auto const& entry : std::filesystem::directory_iterator(existing_profile)) {
      if (entry.is_directory() && entry.path().filename().string().starts_with("debug-run-")) { ++before; }
    }
    auto pi = StartDemo(DemoExe("win32_minimal_app.exe"), existing_profile);
    HWND main = WaitMainWindow(pi.dwProcessId, std::chrono::seconds{30});
    CHECK(main != nullptr);
    CHECK(WaitForMainWindowNamedControls(main, std::chrono::seconds{15}));
    CHECK(!MainWindowShowsAppliedState(main));
    CHECK(ListSelection(main) == -1);
    SelectListRow(main, 1);
    ClickApplyButton(main);
    WaitMainWindowAppliedState(main);
    CloseGracefully(main, pi);
    CHECK(ReadAllBytes(existing_profile / "user.bin") == previous_user);
    std::size_t after = 0;
    for (auto const& entry : std::filesystem::directory_iterator(existing_profile)) {
      if (entry.is_directory() && entry.path().filename().string().starts_with("debug-run-")) {
        ++after;
        CHECK(std::filesystem::exists(entry.path() / "user" / "user.bin"));
        CHECK(std::filesystem::exists(entry.path() / "embedded_distilled_minimal_app_supply.h"));
      }
    }
    CHECK(after == before + 1);
  }
}
#endif

void RunAcceptanceAndRequiredGraphNegatives() {
  auto const good_state = RunAcceptance();
#if defined(APPTRAVERSE_TEST_DEBUG_BOOTSTRAP)
  TestDebugAlwaysCreatesFreshGraph(good_state);
#endif
#if !defined(APPTRAVERSE_MINIMAL_APP_USE_EMBEDDED_DISTILL)
  RunRequiredGraphNegativeTests(
      good_state, DemoExe("win32_minimal_app_load_only.exe"));
#endif
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::example::minimal::RegisterMinimalApplicationTypes();
  apptraverse::test::RunAcceptanceAndRequiredGraphNegatives();
  std::cout << "minimal_app_win32_acceptance_test OK\n";
  return 0;
}
