#include "apptraverse/model_runtime.h"

#include <algorithm>

#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {
namespace {

void ModelRuntimeMaterializedChange(void* ctx, Node& node) {
  static_cast<ModelRuntime*>(ctx)->NotifyMaterializedChange(node);
}

}  // namespace

ModelRuntime::ModelRuntime(ae::Obj& application_root, UiMirror& ui_mirror)
    : application_root_{application_root}, ui_mirror_{ui_mirror} {
  BuildExecutionLists();
}

ModelRuntime::~ModelRuntime() {
  RequestStop();
  Join();
  ClearReachableNodesMaterializedChangeNotifier(application_root_);
  for (Node* node : model_nodes_) {
    node->ClearMaterializedChangeNotifier();
  }
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

void ModelRuntime::AttachNode(Node& node, ae::Obj& presentation_root) {
  // Distilled / already-journaled Nodes already have base+journal; only new
  // Nodes need InitializeRuntimeNode. Always (re)map for publication.
  if (!node.base.is_valid()) {
    InitializeRuntimeNode(node);
  }
  // Only the live Node is bound. Node::base is a historical snapshot that
  // CaptureBaseState / CompactJournal replace, so a notifier on it would
  // outlive the object it points at.
  node.BindMaterializedChangeNotifier(this, &ModelRuntimeMaterializedChange);
  if (std::find(model_nodes_.begin(), model_nodes_.end(), &node) ==
      model_nodes_.end()) {
    model_nodes_.push_back(&node);
  }
  auto const root_id = presentation_root.obj_id.id();
  auto& roots = object_to_roots_[node.obj_id.id()];
  if (std::find(roots.begin(), roots.end(), root_id) == roots.end()) {
    roots.push_back(root_id);
  }
}

void ModelRuntime::DetachNode(Node& node, ae::Obj& presentation_root) {
  auto const node_id = node.obj_id.id();
  auto const root_id = presentation_root.obj_id.id();

  model_nodes_.erase(
      std::remove(model_nodes_.begin(), model_nodes_.end(), &node),
      model_nodes_.end());
  node.ClearMaterializedChangeNotifier();

  auto roots_it = object_to_roots_.find(node_id);
  if (roots_it != object_to_roots_.end()) {
    auto& roots = roots_it->second;
    roots.erase(std::remove(roots.begin(), roots.end(), root_id), roots.end());
    if (roots.empty()) {
      object_to_roots_.erase(roots_it);
    }
  }

  auto pending_it = pending_by_root_.find(root_id);
  if (pending_it != pending_by_root_.end()) {
    pending_it->second.erase(&node);
    if (pending_it->second.empty()) {
      pending_by_root_.erase(pending_it);
    }
  }
}

void ModelRuntime::BuildExecutionLists() {
  CollectReachableNodes(application_root_, model_nodes_);
}

void ModelRuntime::OnMaterializedChange(Node& node) {
  auto it = object_to_roots_.find(node.obj_id.id());
  if (it == object_to_roots_.end()) {
    return;
  }
  for (std::uint32_t root_id : it->second) {
    pending_by_root_[root_id].insert(&node);
  }
}

bool ModelRuntime::HasPending(std::uint32_t root_id) const {
  auto it = pending_by_root_.find(root_id);
  return it != pending_by_root_.end() && !it->second.empty();
}

bool ModelRuntime::IsInExecutionList(Node const& node) const {
  return std::find(model_nodes_.begin(), model_nodes_.end(), &node) !=
         model_nodes_.end();
}

bool ModelRuntime::IsMappedToPresentationRoot(Node const& node,
                                              std::uint32_t root_id) const {
  auto it = object_to_roots_.find(node.obj_id.id());
  if (it == object_to_roots_.end()) {
    return false;
  }
  return std::find(it->second.begin(), it->second.end(), root_id) !=
         it->second.end();
}

void ModelRuntime::SetUpdateObserver(UpdateObserver observer) {
  update_observer_ = std::move(observer);
}

void ModelRuntime::BindAllModelNodeNotifiers() {
  for (Node* node : model_nodes_) {
    node->BindMaterializedChangeNotifier(this, &ModelRuntimeMaterializedChange);
  }
}

void ModelRuntime::Start() {
  stop_ = false;
  accept_work_ = true;
  BindAllModelNodeNotifiers();
  thread_ = std::thread([this] { ThreadMain(); });
}

void ModelRuntime::RequestStop() {
  // Closing the acceptance window and raising stop happen in the same critical
  // section that Post and DrainWork use, so a Post either lands before the
  // boundary (and is drained after the loop exits) or is rejected.
  {
    std::lock_guard<std::mutex> lock{mu_};
    accept_work_ = false;
    stop_ = true;
  }
  cv_.notify_all();
}

void ModelRuntime::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ModelRuntime::Post(Work work) {
  {
    std::lock_guard<std::mutex> lock{mu_};
    // The accept decision and the push are one step. Reading accept_work_
    // outside the lock let a Post that observed an open window push after
    // ThreadMain's final DrainWork, leaving the work queued and never run.
    if (!accept_work_.load()) {
      return;
    }
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
  BindAllModelNodeNotifiers();
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
    if (update_observer_) {
      update_observer_(*node);
    }
  }
}

void ModelRuntime::PublishChanged() {
  for (auto it = pending_by_root_.begin(); it != pending_by_root_.end();) {
    if (it->second.empty()) {
      it = pending_by_root_.erase(it);
      continue;
    }
    std::vector<Node*> changed(it->second.begin(), it->second.end());
    if (ui_mirror_.Publish(it->first, changed)) {
      it = pending_by_root_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace apptraverse
