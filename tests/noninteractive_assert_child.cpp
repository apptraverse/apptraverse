#include <cassert>
#include <cstdio>

int main() {
  std::fputs("NONINTERACTIVE_ASSERT_CHILD\n", stderr);
  std::fflush(stderr);
  assert(!"noninteractive assert regression");
}
