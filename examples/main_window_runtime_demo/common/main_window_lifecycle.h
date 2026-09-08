#ifndef APPTRAVERSE_MAIN_WINDOW_LIFECYCLE_H_
#define APPTRAVERSE_MAIN_WINDOW_LIFECYCLE_H_

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  ifdef RegisterClass
#    undef RegisterClass
#  endif
#endif

#include "apptraverse/publication_channel.h"

namespace apptraverse {

inline constexpr unsigned WM_APPTRAVERSE_PUBLISHED = 0x8001;  // WM_APP + 1
inline constexpr unsigned WM_APPTRAVERSE_STOP = 0x8002;       // WM_APP + 2

// Model-thread path shared by the Win32 app and headless tests. GUI may only
// touch the publication channel, RequestStop, notify_hwnd, and done_event.
struct ModelSession {
  std::filesystem::path state_dir;
  PublicationChannel<3> channel;
  std::mutex mu;
  std::condition_variable cv;
  bool stop{false};
#ifdef _WIN32
  HWND notify_hwnd{nullptr};
  HANDLE done_event{nullptr};
#endif

  void RequestStop();
  void Run();
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_LIFECYCLE_H_
