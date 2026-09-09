#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(cond)                                                         \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

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

int main() {
  auto err_path = std::filesystem::temp_directory_path() /
                  "apptraverse_win32_fatal_ndebug.stderr.txt";
  std::filesystem::remove(err_path);

  std::filesystem::path child{WIN32_FATAL_NDEBUG_CHILD};
  std::wstring cmd = L"\"" + child.wstring() + L"\"";
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
  BOOL ok = CreateProcessW(child.wstring().c_str(), buf.data(), nullptr,
                             nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &si, &pi);
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
  if (err.find("fatal: RegisterClassW Main GetLastError=5") ==
      std::string::npos) {
    std::cerr << "FatalWin32 NDEBUG stderr was:\n" << err << '\n';
    std::exit(1);
  }
  CHECK(CountNeedle(err, "fatal: RegisterClassW Main GetLastError=5") == 1);
  CHECK(err.find("executed after FatalWin32") == std::string::npos);
  std::filesystem::remove(err_path);
  std::cout << "win32_fatal_ndebug_test OK\n";
  return 0;
}
