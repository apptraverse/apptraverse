#ifndef APPTRAVERSE_MODEL_RUNTIME_H_
#define APPTRAVERSE_MODEL_RUNTIME_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
  ~ModelRuntime();

  void AddPresentationRoot(ae::Obj& root);
  void Start();
  void RequestStop();
  void Join();
  void Post(Work work);
  void PumpOnce(std::chrono::steady_clock::time_point now);

  ae::Obj& application() { return application_root_; }

  bool HasPending(std::uint32_t root_id) const;

 private:
  void ThreadMain();
  void DrainWork();
  void UpdateAll(std::chrono::steady_clock::time_point now);
  void PublishChanged();
  void OnMaterializedChange(Node& node);
  void BuildExecutionLists();

  ae::Obj& application_root_;
  UiMirror& ui_mirror_;
  std::vector<ae::Obj*> presentation_roots_;
  std::vector<Node*> model_nodes_;
  std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> object_to_roots_;
  std::unordered_map<std::uint32_t, std::unordered_set<Node*>> pending_by_root_;

  std::mutex mu_;
  std::condition_variable cv_;
  std::queue<Work> work_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> accept_work_{true};
  std::thread thread_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MODEL_RUNTIME_H_
