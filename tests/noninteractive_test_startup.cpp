#include "apptraverse/noninteractive_crt.h"

// Install before other static initializers in this executable (MSVC).
#ifdef _MSC_VER
#  pragma init_seg(compiler)
#endif

namespace {
struct AppTraverseNoninteractiveTestStartup {
  AppTraverseNoninteractiveTestStartup() {
    apptraverse::EnableNoninteractiveCrt();
  }
};
const AppTraverseNoninteractiveTestStartup apptraverse_noninteractive_test_startup{};
}  // namespace
