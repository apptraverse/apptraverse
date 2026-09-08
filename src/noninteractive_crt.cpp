#include "apptraverse/noninteractive_crt.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <werapi.h>
#  include <cstdlib>
#  include <cstdio>
#  ifdef _MSC_VER
#    include <crtdbg.h>
#    pragma comment(lib, "wer.lib")
#  endif
#endif

namespace apptraverse {
namespace {

#ifdef _WIN32
#  ifdef _MSC_VER
int CrtAssertHook(int report_type, char* message, int* return_value) {
  if (report_type != _CRT_ASSERT) {
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
  return 1;
}
#  endif
#endif

}  // namespace

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
  _CrtSetReportHook(&CrtAssertHook);
#  endif
  WerSetFlags(WER_FAULT_REPORTING_NO_UI);
#endif
}

}  // namespace apptraverse
