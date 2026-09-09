#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "apptraverse/object_macros.h"
#include "main_window_lifecycle.h"

#define CHECK(cond)                                                         \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

int RunMissingLoad(char const* dir) {
  apptraverse::EnsureObjectRegistration();
  apptraverse::ModelSession session;
  session.state_dir = dir;
  session.Run([] {
    std::fputs("unexpected publication after missing load\n", stderr);
    std::fflush(stderr);
    std::abort();
  });
  return 0;
}

std::string ReadAll(std::filesystem::path const& path) {
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

int main(int argc, char** argv) {
  if (argc >= 3 && std::string_view{argv[1]} == "--load-missing") {
    return RunMissingLoad(argv[2]);
  }

  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_main_window_fatal_ndebug";
  auto err_path = dir;
  err_path += L".stderr.txt";
  std::filesystem::remove_all(dir);
  std::filesystem::remove(err_path);
  std::filesystem::create_directories(dir);

  wchar_t self[MAX_PATH]{};
  CHECK(GetModuleFileNameW(nullptr, self, MAX_PATH) != 0);
  std::wstring cmd = std::wstring{L"\""} + self +
                      L"\" --load-missing \"" + dir.wstring() + L"\"";
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(L'\0');

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
  BOOL ok = CreateProcessW(self, buf.data(), nullptr, nullptr, TRUE,
                             CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,
                             nullptr, nullptr, &si, &pi);
  CloseHandle(errf);
  CloseHandle(nul);
  CHECK(ok);
  CloseHandle(pi.hThread);
  CHECK(WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CHECK(code != 0);

  std::string err = ReadAll(err_path);
  if (err.find("fatal: LoadApplication failed") == std::string::npos) {
    std::cerr << "ndebug stderr was:\n" << err << '\n';
    std::exit(1);
  }
  std::filesystem::remove_all(dir);
  std::filesystem::remove(err_path);
  std::cout << "main_window_fatal_ndebug_test OK\n";
  return 0;
}
