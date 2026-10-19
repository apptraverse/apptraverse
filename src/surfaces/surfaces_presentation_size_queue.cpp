#include "apptraverse/surfaces/surfaces_presentation_size_queue.h"

#include <cassert>

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse {
namespace {

SurfacesPresentationSizeQueue* g_bound_queue = nullptr;
std::mutex g_bound_queue_mu;

}  // namespace

void SurfacesPresentationSizeQueue::BindPostWork(PostWork post) {
  std::lock_guard<std::mutex> lock{mu_};
  post_ = std::move(post);
}

void SurfacesPresentationSizeQueue::BindProxy(ModelObjectProxy* proxy) {
  assert(proxy != nullptr);
  std::lock_guard<std::mutex> lock{mu_};
  ++proxy_refs_[proxy];
  std::lock_guard<std::mutex> global{g_bound_queue_mu};
  g_bound_queue = this;
}

void SurfacesPresentationSizeQueue::UnbindProxy(ModelObjectProxy* proxy) {
  std::lock_guard<std::mutex> lock{mu_};
  auto it = proxy_refs_.find(proxy);
  if (it == proxy_refs_.end()) {
    return;
  }
  if (--it->second == 0) {
    proxy_refs_.erase(it);
  }
  if (proxy_refs_.empty()) {
    std::lock_guard<std::mutex> global{g_bound_queue_mu};
    g_bound_queue = nullptr;
  }
}

std::size_t SurfacesPresentationSizeQueue::PendingSurfaceCountForTest() const {
  std::lock_guard<std::mutex> lock{mu_};
  std::size_t count = 0;
  for (auto const& [id, slot] : slots_) {
    (void)id;
    if (slot.queued) {
      ++count;
    }
  }
  return count;
}

void SurfacesPresentationSizeQueue::EnqueueSurfaceWork(ae::ObjId surface_id) {
  assert(post_);
  auto const surface_key = surface_id.id();
  post_([this, surface_key](ae::Domain& domain) {
    for (;;) {
      std::int32_t width = 0;
      std::int32_t height = 0;
      {
        std::lock_guard<std::mutex> lock{mu_};
        auto it = slots_.find(surface_key);
        if (it == slots_.end()) {
          return;
        }
        width = it->second.width;
        height = it->second.height;
      }

      auto object = domain.Find(ae::ObjId{surface_key});
      if (!object) {
        std::lock_guard<std::mutex> lock{mu_};
        slots_.erase(surface_key);
        return;
      }
      {
        std::lock_guard<std::mutex> lock{mu_};
        auto it = slots_.find(surface_key);
        if (it == slots_.end()) {
          return;
        }
        if (it->second.width != width || it->second.height != height) {
          continue;
        }
      }
      auto* surface = object.as<Surface>();
      surface->SetPresentationSize(width, height);

      std::lock_guard<std::mutex> lock{mu_};
      auto it = slots_.find(surface_key);
      if (it == slots_.end()) {
        return;
      }
      if (it->second.width == width && it->second.height == height) {
        slots_.erase(it);
        return;
      }
      // Keep queued=true while this work item drains superseded sizes so UI
      // bursts cannot enqueue parallel model work or commit every intermediate
      // mouse size.
    }
  });
}

void SurfacesPresentationSizeQueue::Post(ModelObjectProxy* proxy,
                                         ae::ObjId surface_id,
                                         std::int32_t width,
                                         std::int32_t height) {
  (void)proxy;
  bool enqueue = false;
  {
    std::lock_guard<std::mutex> lock{mu_};
    assert(post_);
    auto const surface_key = surface_id.id();
    auto& slot = slots_[surface_key];
    slot.width = width;
    slot.height = height;
    if (slot.queued) {
      return;
    }
    slot.queued = true;
    enqueue = true;
  }
  if (enqueue) {
    EnqueueSurfaceWork(surface_id);
  }
}

void PostSurfacePresentationSize(ModelObjectProxy* proxy, ae::ObjId surface_id,
                                 std::int32_t width, std::int32_t height) {
  assert(proxy != nullptr);
  {
    std::lock_guard<std::mutex> lock{g_bound_queue_mu};
    if (g_bound_queue != nullptr) {
      g_bound_queue->Post(proxy, surface_id, width, height);
      return;
    }
  }
  proxy->Invoke<Surface>(surface_id, &Surface::SetPresentationSize, width,
                         height);
}

}  // namespace apptraverse
