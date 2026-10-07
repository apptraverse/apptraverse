#include <cstdio>
#include <cstdlib>

int main() {
  std::fputs("NONINTERACTIVE_ABORT_CHILD\n", stderr);
  std::fflush(stderr);
  std::abort();
}
