#ifndef APPTRAVERSE_MAIN_WINDOW_LIFECYCLE_H_
#define APPTRAVERSE_MAIN_WINDOW_LIFECYCLE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#include "apptraverse/publication_channel.h"

namespace apptraverse {

enum class ModelStartupStage : int {
  None = 0,
  Creating = 1,
  Distilling = 2,
  DestroyingFresh = 3,
  Loading = 4,
  Serializing = 5,
  Ready = 6,
  Stopping = 7,
  Stopped = 8,
};

inline constexpr unsigned WM_APPTRAVERSE_PUBLISHED = 0x8001;  // WM_APP + 1

// Model-thread session. GUI may only touch atomics, the publication channel,
// RequestStop, and the done event. No model Obj*/Domain pointers cross the
// thread boundary.
struct ModelSession {
  std::filesystem::path state_dir;
  PublicationChannel<3> channel;
  std::mutex mu;
  std::condition_variable cv;
  std::atomic<bool> stop{false};
  std::atomic<bool> published{false};
  std::atomic<bool> finished{false};
  std::atomic<bool> distilled_this_run{false};
  std::atomic<int> stage{static_cast<int>(ModelStartupStage::None)};
  std::atomic<int> hold_stage{-1};
  std::atomic<std::thread::id> create_thread{};
  std::atomic<std::thread::id> destroy_thread{};
  std::atomic<std::uintptr_t> notify_hwnd{0};
  std::atomic<std::uintptr_t> model_application_addr{0};
  std::atomic<std::uintptr_t> model_window_addr{0};
  std::atomic<std::uintptr_t> model_domain_addr{0};
  std::atomic<std::uint32_t> model_application_id{0};
  std::atomic<std::uint32_t> model_window_id{0};
  std::atomic<std::int32_t> model_window_x{0};
  std::atomic<std::int32_t> model_window_y{0};
  std::atomic<std::int32_t> model_window_width{0};
  std::atomic<std::int32_t> model_window_height{0};
#ifdef _WIN32
  HANDLE done_event{nullptr};
#endif

  void RequestStop();
  void Run();
};

bool ApplicationStateExists(std::filesystem::path const& dir);

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_LIFECYCLE_H_
