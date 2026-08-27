#include "apptraverse/model_runtime.h"

#include "apptraverse/materialized_ops.h"

namespace apptraverse {

ModelRuntime::ModelRuntime(ae::Obj& application_root, UiMirror& ui_mirror)
    : application_root_{application_root}, ui_mirror_{ui_mirror} {
  BuildExecutionLists();
}

ModelRuntime::~ModelRuntime() {
  RequestStop();
  Join();
  Node::SetMaterializedChangeNotifier({});
}

void ModelRuntime::AddPresentationRoot(ae::Obj& root) {
  presentation_roots_.push_back(&root);
  std::vector<ae::Obj*> objects;
  CollectReachableObjects(root, objects);
  auto const root_id = root.obj_id.id();
  for (ae::Obj* object : objects) {
    object_to_roots_[object->obj_id.id()].push_back(root_id);
  }
}

void ModelRuntime::BuildExecutionLists() {
  CollectReachableNodes(application_root_, model_nodes_);
}

void ModelRuntime::OnMaterializedChange(Node& node) {
  if (!changed_set_.insert(&node).second) {
    return;
  }
  changed_nodes_.push_back(&node);
}

void ModelRuntime::Start() {
  stop_ = false;
  accept_work_ = true;
  Node::SetMaterializedChangeNotifier(
      [this](Node& node) { OnMaterializedChange(node); });
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
  Node::SetMaterializedChangeNotifier({});
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
    PublishChanged();
    std::unique_lock<std::mutex> lock{mu_};
    cv_.wait_until(lock, next_update, [this] {
      return stop_.load() || !work_.empty();
    });
  }
  DrainWork();
}

void ModelRuntime::PumpOnce(std::chrono::steady_clock::time_point now) {
  Node::SetMaterializedChangeNotifier(
      [this](Node& node) { OnMaterializedChange(node); });
  DrainWork();
  UpdateAll(now);
  PublishChanged();
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
  for (Node* node : model_nodes_) {
    node->EnsureCurrentGeneration();
    node->Update(now);
  }
}

void ModelRuntime::PublishChanged() {
  if (changed_nodes_.empty()) {
    return;
  }

  std::unordered_map<std::uint32_t, std::vector<Node*>> by_root;
  for (Node* node : changed_nodes_) {
    auto it = object_to_roots_.find(node->obj_id.id());
    if (it == object_to_roots_.end()) {
      continue;
    }
    for (std::uint32_t root_id : it->second) {
      by_root[root_id].push_back(node);
    }
  }
  changed_nodes_.clear();
  changed_set_.clear();

  for (auto& entry : by_root) {
    ui_mirror_.Publish(entry.first, entry.second);
  }
}

}  // namespace apptraverse
