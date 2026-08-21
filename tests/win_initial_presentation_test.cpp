// Focused smoke: fresh event-driven Windows chat must show Join in the
// Win32 transcript EDIT before any Send/Add. Also checks restart restore.
//
// Usage:
//   apptraverse_win_initial_presentation_test.exe --exe <path-to-win32_single_client_chat>
//
// This test must FAIL on builds where business publishes before HWND exists.

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr UINT kWmClose = WM_CLOSE;
constexpr UINT kWmGetText = WM_GETTEXT;
constexpr UINT kWmGetTextLength = WM_GETTEXTLENGTH;
constexpr int kGwChild = 5;
constexpr int kGwHwndNext = 2;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

std::wstring Utf8ToWide(std::string const& utf8) {
  if (utf8.empty()) {
    return {};
  }
  int const size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                       static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                      out.data(), size);
  return out;
}

std::string WideToUtf8(std::wstring const& wide) {
  if (wide.empty()) {
    return {};
  }
  int const size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                       static_cast<int>(wide.size()), nullptr, 0,
                                       nullptr, nullptr);
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

struct EnumCtx {
  std::wstring needle;
  HWND found{nullptr};
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lparam) {
  auto* ctx = reinterpret_cast<EnumCtx*>(lparam);
  wchar_t title[512]{};
  GetWindowTextW(hwnd, title, 512);
  if (wcsstr(title, ctx->needle.c_str()) != nullptr && IsWindowVisible(hwnd)) {
    ctx->found = hwnd;
    return FALSE;
  }
  return TRUE;
}

HWND FindWindowSubstring(std::wstring const& needle, double timeout_s) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_s);
  while (std::chrono::steady_clock::now() < deadline) {
    EnumCtx ctx{needle, nullptr};
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.found != nullptr) {
      return ctx.found;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
  }
  return nullptr;
}

std::string GetTranscriptUtf8(HWND hwnd) {
  HWND child = GetWindow(hwnd, kGwChild);
  while (child != nullptr) {
    wchar_t cls[256]{};
    GetClassNameW(child, cls, 256);
    if (_wcsicmp(cls, L"EDIT") == 0) {
      LONG const style = GetWindowLongW(child, GWL_STYLE);
      if ((style & ES_MULTILINE) != 0) {
        LRESULT const length =
            SendMessageW(child, kWmGetTextLength, 0, 0);
        std::wstring buf(static_cast<std::size_t>(length) + 1, L'\0');
        SendMessageW(child, kWmGetText, length + 1,
                     reinterpret_cast<LPARAM>(buf.data()));
        buf.resize(static_cast<std::size_t>(length));
        return WideToUtf8(buf);
      }
    }
    child = GetWindow(child, kGwHwndNext);
  }
  return {};
}

std::string WaitTranscriptContains(HWND hwnd, char const* needle,
                                   double timeout_s) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_s);
  std::string last;
  while (std::chrono::steady_clock::now() < deadline) {
    last = GetTranscriptUtf8(hwnd);
    if (last.find(needle) != std::string::npos) {
      return last;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
  }
  return last;
}

bool RunProcess(std::wstring const& exe, std::wstring const& args,
                std::wstring const& instance, DWORD* exit_code,
                double timeout_s) {
  std::wstring cmdline = L"\"" + exe + L"\" " + args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring env_block;
  {
    // Inherit current env and override APPTRAVERSE_INSTANCE / VERBOSE.
    wchar_t* inherited = GetEnvironmentStringsW();
    CHECK(inherited != nullptr);
    for (wchar_t* p = inherited; *p != L'\0';) {
      std::wstring entry = p;
      p += entry.size() + 1;
      if (entry.rfind(L"APPTRAVERSE_INSTANCE=", 0) == 0 ||
          entry.rfind(L"APPTRAVERSE_VERBOSE_LOG=", 0) == 0) {
        continue;
      }
      env_block += entry;
      env_block.push_back(L'\0');
    }
    FreeEnvironmentStringsW(inherited);
    env_block += L"APPTRAVERSE_INSTANCE=" + instance;
    env_block.push_back(L'\0');
    env_block += L"APPTRAVERSE_VERBOSE_LOG=0";
    env_block.push_back(L'\0');
    env_block.push_back(L'\0');
  }

  BOOL ok = CreateProcessW(
      nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT,
      env_block.data(), nullptr, &si, &pi);
  if (!ok) {
    std::cerr << "CreateProcess failed err=" << GetLastError() << '\n';
    return false;
  }
  DWORD const wait_ms = static_cast<DWORD>(timeout_s * 1000.0);
  DWORD const wr = WaitForSingleObject(pi.hProcess, wait_ms);
  if (wr != WAIT_OBJECT_0) {
    TerminateProcess(pi.hProcess, 9);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return false;
  }
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  if (exit_code != nullptr) {
    *exit_code = code;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

struct LiveProc {
  PROCESS_INFORMATION pi{};
  bool ok{false};
};

LiveProc StartProcess(std::wstring const& exe, std::wstring const& args,
                      std::wstring const& instance) {
  LiveProc live{};
  std::wstring cmdline = L"\"" + exe + L"\" " + args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  std::wstring env_block;
  {
    wchar_t* inherited = GetEnvironmentStringsW();
    CHECK(inherited != nullptr);
    for (wchar_t* p = inherited; *p != L'\0';) {
      std::wstring entry = p;
      p += entry.size() + 1;
      if (entry.rfind(L"APPTRAVERSE_INSTANCE=", 0) == 0 ||
          entry.rfind(L"APPTRAVERSE_VERBOSE_LOG=", 0) == 0) {
        continue;
      }
      env_block += entry;
      env_block.push_back(L'\0');
    }
    FreeEnvironmentStringsW(inherited);
    env_block += L"APPTRAVERSE_INSTANCE=" + instance;
    env_block.push_back(L'\0');
    env_block += L"APPTRAVERSE_VERBOSE_LOG=0";
    env_block.push_back(L'\0');
    env_block.push_back(L'\0');
  }
  live.ok = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                           CREATE_UNICODE_ENVIRONMENT, env_block.data(), nullptr,
                           &si, &live.pi) != 0;
  return live;
}

DWORD CloseViaWmClose(LiveProc& live, HWND hwnd, double timeout_s) {
  if (hwnd != nullptr) {
    PostMessageW(hwnd, kWmClose, 0, 0);
  }
  DWORD const wait_ms = static_cast<DWORD>(timeout_s * 1000.0);
  if (WaitForSingleObject(live.pi.hProcess, wait_ms) != WAIT_OBJECT_0) {
    TerminateProcess(live.pi.hProcess, 9);
    WaitForSingleObject(live.pi.hProcess, 5000);
  }
  DWORD code = 1;
  GetExitCodeProcess(live.pi.hProcess, &code);
  CloseHandle(live.pi.hThread);
  CloseHandle(live.pi.hProcess);
  live.ok = false;
  return code;
}

std::wstring ParseExeArg(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--exe") == 0) {
      return Utf8ToWide(argv[i + 1]);
    }
  }
  if (char const* env = std::getenv("APPTRAVERSE_WIN32_CHAT_EXE")) {
    if (env[0] != '\0') {
      return Utf8ToWide(env);
    }
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  auto const exe = ParseExeArg(argc, argv);
  CHECK(!exe.empty());
  CHECK(std::filesystem::exists(std::filesystem::path(exe)));

  auto const root = std::filesystem::temp_directory_path() /
                    ("apptraverse-win-initial-join-" +
                     std::to_string(GetCurrentProcessId()));
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "state", ec);
  CHECK(!ec);

  auto const state = root / "state";
  auto const state_w = Utf8ToWide(state.string());
  auto const exe_args_common =
      std::wstring(L"--event-driven-runtime --state-dir \"") + state_w +
      L"\" --aether-client-name join-smoke";

  DWORD distill_code = 1;
  CHECK(RunProcess(exe, exe_args_common + L" --distill", L"ij", &distill_code,
                   120.0));
  CHECK(distill_code == 0);

  // Fresh run: no Send / Add — only read transcript.
  auto live = StartProcess(exe, exe_args_common, L"ij");
  CHECK(live.ok);
  HWND hwnd = FindWindowSubstring(L"AppTraverse Chat [ij]", 45.0);
  CHECK(hwnd != nullptr);

  auto const fresh_text =
      WaitTranscriptContains(hwnd, "* Windows joined", 15.0);
  std::cout << "FRESH_TRANSCRIPT_BEGIN\n" << fresh_text
            << "\nFRESH_TRANSCRIPT_END\n";
  CHECK(fresh_text.find("* Windows joined") != std::string::npos);

  auto const t0 = std::chrono::steady_clock::now();
  DWORD const close1 = CloseViaWmClose(live, hwnd, 15.0);
  auto const close_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                t0)
          .count();
  CHECK(close1 == 0);
  CHECK(close_ms < 1000.0);

  // Restart same state — Join/history must appear without user action.
  live = StartProcess(exe, exe_args_common, L"ijr");
  CHECK(live.ok);
  hwnd = FindWindowSubstring(L"AppTraverse Chat [ijr]", 45.0);
  CHECK(hwnd != nullptr);
  auto const restart_text =
      WaitTranscriptContains(hwnd, "* Windows joined", 15.0);
  std::cout << "RESTART_TRANSCRIPT_BEGIN\n" << restart_text
            << "\nRESTART_TRANSCRIPT_END\n";
  CHECK(restart_text.find("* Windows joined") != std::string::npos);

  DWORD const close2 = CloseViaWmClose(live, hwnd, 15.0);
  CHECK(close2 == 0);

  std::filesystem::remove_all(root, ec);
  std::cout << "win_initial_presentation_test OK close1_ms=" << close_ms << '\n';
  return 0;
}
