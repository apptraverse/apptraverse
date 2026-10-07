#include "apptraverse/noninteractive_crt.h"

#include <cstdio>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <io.h>
#  include <windows.h>
#  include <werapi.h>
#  include <cstdlib>
#  ifdef _MSC_VER
#    include <crtdbg.h>
#    pragma comment(lib, "wer.lib")
#  endif
#endif

namespace apptraverse {
namespace {

#ifdef _WIN32
#  ifdef _MSC_VER
void InvalidParameterHandler(wchar_t const*, wchar_t const*, wchar_t const*,
                             unsigned int, uintptr_t) {
  apptraverse::WriteFatalStderr("CRT invalid parameter\n");
  std::_Exit(4);
}

void PurecallHandler() {
  apptraverse::WriteFatalStderr("CRT pure virtual call\n");
  std::_Exit(5);
}

int CrtReportHook(int report_type, char* message, int* return_value) {
  if (report_type != _CRT_ASSERT && report_type != _CRT_ERROR) {
    return 0;
  }
  if (message != nullptr) {
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
  }
  if (return_value != nullptr) {
    *return_value = 0;
  }
  std::_Exit(3);
}
#  endif
#endif

}  // namespace

void WriteFatalStderr(char const* text) {
  if (text == nullptr) {
    return;
  }
#ifdef _WIN32
  // One write. CRT stderr and STD_ERROR_HANDLE are often the same pipe;
  // writing both duplicates the diagnostic. Prefer CRT when it is attached;
  // otherwise the Win32 handle (GUI-subsystem child with redirected stderr).
  int const fd = _fileno(stderr);
  HANDLE crt = INVALID_HANDLE_VALUE;
  if (fd >= 0) {
    crt = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  }
  HANDLE const win = GetStdHandle(STD_ERROR_HANDLE);
  if (crt != nullptr && crt != INVALID_HANDLE_VALUE) {
    std::fputs(text, stderr);
    std::fflush(stderr);
    return;
  }
  if (win != nullptr && win != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(win, text, static_cast<DWORD>(lstrlenA(text)), &written, nullptr);
    FlushFileBuffers(win);
  }
#else
  std::fputs(text, stderr);
  std::fflush(stderr);
#endif
}

void EnableNoninteractiveCrt() {
#ifdef _WIN32
  _set_error_mode(_OUT_TO_STDERR);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                SEM_NOOPENFILEERRORBOX);
#  ifdef _MSC_VER
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportHook(&CrtReportHook);
  _set_invalid_parameter_handler(&InvalidParameterHandler);
  _set_purecall_handler(&PurecallHandler);
#  endif
  WerSetFlags(WER_FAULT_REPORTING_NO_UI);
#endif
}

}  // namespace apptraverse
