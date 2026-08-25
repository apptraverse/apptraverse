#include "apptraverse/model_runtime.h"

#include "apptraverse/graph_walk.h"
#include "apptraverse/materialized_ops.h"

namespace apptraverse {

ModelRuntime::ModelRuntime(ae::Obj& application_root, UiMirror& ui_mirror)
    : application_root_{application_root}, ui_mirror_{ui_mirror} {}

void ModelRuntime::AddPresentationRoot(ae::Obj& root) {
  presentation_roots_.push_back(&root);
}

void ModelRuntime::Start() {
  stop_ = false;
  accept_work_ = true;
  thread_ = std::thread([this] { ThreadMain(); });
}

void ModelRuntime::RequestStop() {
  accept_work_ = false;
  stop_ = true;
  cv_.notify_all();
}

void ModelRuntime::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ModelRuntime::Post(Work work) {
  if (!accept_work_.load()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock{mu_};
    work_.push(std::move(work));
  }
  cv_.notify_one();
}

void ModelRuntime::ThreadMain() {
  auto next_update =
      std::chrono::steady_clock::now() + kDefaultModelUpdatePeriod;
  while (!stop_.load()) {
    DrainWork();
    auto const now = std::chrono::steady_clock::now();
    if (now >= next_update) {
      UpdateAll(now);
      next_update = now + kDefaultModelUpdatePeriod;
    }
    PublishRoots();
    std::unique_lock<std::mutex> lock{mu_};
    cv_.wait_until(lock, next_update, [this] {
      return stop_.load() || !work_.empty();
    });
  }
  DrainWork();
}

void ModelRuntime::PumpOnce(std::chrono::steady_clock::time_point now) {
  DrainWork();
  UpdateAll(now);
  PublishRoots();
}

void ModelRuntime::DrainWork() {
  for (;;) {
    Work work;
    {
      std::lock_guard<std::mutex> lock{mu_};
      if (work_.empty()) {
        return;
      }
      work = std::move(work_.front());
      work_.pop();
    }
    work();
  }
}

void ModelRuntime::UpdateAll(std::chrono::steady_clock::time_point now) {
  std::vector<Node*> nodes;
  CollectReachableNodes(application_root_, nodes);
  for (Node* node : nodes) {
    node->EnsureCurrentGeneration();
    node->Update(now);
  }
}

void ModelRuntime::PublishRoots() {
  for (ae::Obj* root : presentation_roots_) {
    ui_mirror_.Publish(*root);
  }
}

}  // namespace apptraverse
