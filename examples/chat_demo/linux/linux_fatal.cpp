#include "linux_fatal.h"

#include <cstdio>
#include <cstdlib>

namespace apptraverse::example::chat_demo {

[[noreturn]] void FatalLinux(char const* operation) {
  std::fprintf(stderr, "FATAL: %s\n", operation);
  std::abort();
}

}  // namespace apptraverse::example::chat_demo
