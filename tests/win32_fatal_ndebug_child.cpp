#include "apptraverse/noninteractive_crt.h"
#include "win32_fatal.h"

#include <cstdio>

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::FatalWin32("RegisterClassW Main", 5);
  std::fputs("executed after FatalWin32\n", stderr);
  std::fflush(stderr);
  return 0;
}
