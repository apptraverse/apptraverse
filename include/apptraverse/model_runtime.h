#ifndef APPTRAVERSE_MODEL_RUNTIME_H_
#define APPTRAVERSE_MODEL_RUNTIME_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "aether/obj/obj.h"

#include "apptraverse/node.h"
#include "apptraverse/ui_mirror.h"

namespace apptraverse {

inline constexpr auto kDefaultModelUpdatePeriod = std::chrono::milliseconds{16};

class ModelRuntime {
 public:
  using Work = std::function<void()>;

  explicit ModelRuntime(ae::Obj& application_root, UiMirror& ui_mirror);

  void AddPresentationRoot(ae::Obj& root);
  void Start();
  void RequestStop();
  void Join();
  void Post(Work work);
  void PumpOnce(std::chrono::steady_clock::time_point now);

  ae::Obj& application() { return application_root_; }

 private:
  void ThreadMain();
  void DrainWork();
  void UpdateAll(std::chrono::steady_clock::time_point now);
  void PublishRoots();

  ae::Obj& application_root_;
  UiMirror& ui_mirror_;
  std::vector<ae::Obj*> presentation_roots_;

  std::mutex mu_;
  std::condition_variable cv_;
  std::queue<Work> work_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> accept_work_{true};
  std::thread thread_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MODEL_RUNTIME_H_
