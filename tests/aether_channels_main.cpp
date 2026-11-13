#include <unity.h>
#include <cstdio>
#include <cstdlib>
#ifdef _MSC_VER
#  include <crtdbg.h>
#endif
#include "apptraverse/noninteractive_crt.h"

void setUp() {}
void tearDown() {}
int run_test_cloud_persistence();
int main() {
  apptraverse::EnableNoninteractiveCrt();
#ifdef _MSC_VER
  _CrtSetReportHook([](int, char* message, int*) -> int {
    std::fprintf(stderr, "SDK test %s: %s\n", Unity.CurrentTestName, message);
    std::fflush(stderr);
    std::_Exit(3);
  });
#endif
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  return run_test_cloud_persistence();
}
