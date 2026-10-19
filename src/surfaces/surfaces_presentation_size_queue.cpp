#include "apptraverse/surfaces/surfaces_presentation_size_queue.h"

#include <cassert>
#include <vector>

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse {

void SurfacesPresentationSizeQueue::BindPostWork(PostWork post) {
  std::lock_guard<std::mutex> lock{mu_};
  post_ = std::move(post);
}

void SurfacesPresentationSizeQueue::BindProxy(ModelObjectProxy* proxy) {
  assert(proxy != nullptr);
  std::lock_guard<std::mutex> lock{mu_};
  ++proxy_refs_[proxy];
  proxy->BindPresentationSizeQueue(this);
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
  proxy->UnbindPresentationSizeQueue(this);
}

std::size_t SurfacesPresentationSizeQueue::PendingSurfaceCountForTest() const {
  std::lock_guard<std::mutex> lock{mu_};
  return slots_.size();
}

void SurfacesPresentationSizeQueue::EnsureDrainScheduled() {
  {
    std::lock_guard<std::mutex> lock{mu_};
    assert(post_);
    if (drain_scheduled_) {
      return;
    }
    drain_scheduled_ = true;
  }
  post_([this](ae::Domain& domain) {
    {
      std::lock_guard<std::mutex> lock{mu_};
      drain_scheduled_ = false;
    }
    std::vector<std::uint32_t> surface_keys;
    {
      std::lock_guard<std::mutex> lock{mu_};
      surface_keys.reserve(slots_.size());
      for (auto const& [key, slot] : slots_) {
        (void)slot;
        surface_keys.push_back(key);
      }
    }

    bool need_again = false;
    for (std::uint32_t surface_key : surface_keys) {
      std::int32_t width = 0;
      std::int32_t height = 0;
      {
        std::lock_guard<std::mutex> lock{mu_};
        auto it = slots_.find(surface_key);
        if (it == slots_.end()) {
          continue;
        }
        width = it->second.width;
        height = it->second.height;
      }

      auto object = domain.Find(ae::ObjId{surface_key});
      if (!object) {
        std::lock_guard<std::mutex> lock{mu_};
        slots_.erase(surface_key);
        continue;
      }

      {
        std::lock_guard<std::mutex> lock{mu_};
        auto it = slots_.find(surface_key);
        if (it == slots_.end()) {
          continue;
        }
        if (it->second.width != width || it->second.height != height) {
          width = it->second.width;
          height = it->second.height;
        }
      }

      auto* surface = object.as<Surface>();
      surface->SetPresentationSize(width, height);

      {
        std::lock_guard<std::mutex> lock{mu_};
        auto it = slots_.find(surface_key);
        if (it == slots_.end()) {
          continue;
        }
        if (it->second.width == width && it->second.height == height) {
          slots_.erase(it);
          continue;
        }
        need_again = true;
      }
    }

    if (need_again) {
      EnsureDrainScheduled();
    }
  });
}

void SurfacesPresentationSizeQueue::Post(ModelObjectProxy* proxy,
                                         ae::ObjId surface_id,
                                         std::int32_t width,
                                         std::int32_t height) {
  (void)proxy;
  {
    std::lock_guard<std::mutex> lock{mu_};
    assert(post_);
    auto const surface_key = surface_id.id();
    auto& slot = slots_[surface_key];
    slot.width = width;
    slot.height = height;
  }
  EnsureDrainScheduled();
}

void PostSurfacePresentationSize(ModelObjectProxy* proxy, ae::ObjId surface_id,
                                 std::int32_t width, std::int32_t height) {
  assert(proxy != nullptr);
  if (SurfacesPresentationSizeQueue* queue = proxy->PresentationSizeQueue()) {
    queue->Post(proxy, surface_id, width, height);
    return;
  }
  proxy->Invoke<Surface>(surface_id, &Surface::SetPresentationSize, width,
                         height);
}

}  // namespace apptraverse
