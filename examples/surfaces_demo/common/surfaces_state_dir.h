#ifndef APPTRAVERSE_EXAMPLES_SURFACES_DEMO_SURFACES_STATE_DIR_H_
#define APPTRAVERSE_EXAMPLES_SURFACES_DEMO_SURFACES_STATE_DIR_H_

#include <filesystem>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#elif defined(__linux__)
#  include <limits.h>
#  include <unistd.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <climits>
#  include <cstdint>
#endif

namespace apptraverse::surfaces_demo {

inline std::filesystem::path DefaultRuntimeStateDir() {
  std::filesystem::path exe_dir = std::filesystem::current_path();
#if defined(_WIN32)
  wchar_t buf[MAX_PATH];
  DWORD const n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n != 0 && n < MAX_PATH) {
    exe_dir = std::filesystem::path(buf).parent_path();
  }
#elif defined(__linux__)
  char buf[PATH_MAX];
  ssize_t const n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = '\0';
    exe_dir = std::filesystem::path(buf).parent_path();
  }
#elif defined(__APPLE__)
  char buf[PATH_MAX];
  std::uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0) {
    exe_dir = std::filesystem::weakly_canonical(std::filesystem::path(buf))
                  .parent_path();
  }
#endif
  return exe_dir / "surfaces_runtime_state";
}

}  // namespace apptraverse::surfaces_demo

#endif  // APPTRAVERSE_EXAMPLES_SURFACES_DEMO_SURFACES_STATE_DIR_H_
