#include "linux_fatal.h"

#include <cstdio>
#include <cstdlib>

#include "apptraverse/noninteractive_crt.h"

namespace apptraverse {

[[noreturn]] void FatalLinux(char const* operation) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), "fatal: %s\n", operation);
  WriteFatalStderr(buf);
  std::abort();
}

}  // namespace apptraverse
