#include "win32_fatal.h"

#include <cstdio>
#include <cstdlib>

#include "apptraverse/noninteractive_crt.h"

namespace apptraverse {

[[noreturn]] void FatalWin32(char const* operation, DWORD error) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), "fatal: %s GetLastError=%lu\n", operation,
                static_cast<unsigned long>(error));
  WriteFatalStderr(buf);
  std::abort();
}

}  // namespace apptraverse
