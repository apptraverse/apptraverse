#include "apptraverse/presentation_size_queue.h"

#include <cassert>
#include <vector>

namespace apptraverse {

void PresentationSizeQueue::BindPostWork(PostWork post) {
  std::lock_guard<std::mutex> lock{mu_};
  post_ = std::move(post);
}

void PresentationSizeQueue::BindSizeApplier(SizeApplier applier) {
  std::lock_guard<std::mutex> lock{mu_};
  size_applier_ = std::move(applier);
}

void PresentationSizeQueue::BindProxy(ModelObjectProxy* proxy) {
  assert(proxy != nullptr);
  std::lock_guard<std::mutex> lock{mu_};
  ++proxy_refs_[proxy];
  proxy->BindPresentationSizeQueue(this);
}

void PresentationSizeQueue::UnbindProxy(ModelObjectProxy* proxy) {
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

std::size_t PresentationSizeQueue::PendingObjectCountForTest() const {
  std::lock_guard<std::mutex> lock{mu_};
  return slots_.size();
}

void PresentationSizeQueue::EnsureDrainScheduled() {
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
    SizeApplier applier;
    {
      std::lock_guard<std::mutex> lock{mu_};
      applier = size_applier_;
    }
    assert(applier);

    std::vector<std::uint32_t> object_keys;
    {
      std::lock_guard<std::mutex> lock{mu_};
      object_keys.reserve(slots_.size());
      for (auto const& [key, slot] : slots_) {
        (void)slot;
        object_keys.push_back(key);
      }
    }

    bool need_again = false;
    for (std::uint32_t object_key : object_keys) {
      std::int32_t width = 0;
      std::int32_t height = 0;
      {
        std::lock_guard<std::mutex> lock{mu_};
        auto it = slots_.find(object_key);
        if (it == slots_.end()) {
          continue;
        }
        width = it->second.width;
        height = it->second.height;
      }

      {
        std::lock_guard<std::mutex> lock{mu_};
        auto it = slots_.find(object_key);
        if (it == slots_.end()) {
          continue;
        }
        if (it->second.width != width || it->second.height != height) {
          width = it->second.width;
          height = it->second.height;
        }
      }

      applier(domain, ae::ObjId{object_key}, width, height);

      {
        std::lock_guard<std::mutex> lock{mu_};
        auto it = slots_.find(object_key);
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

void PresentationSizeQueue::Post(ModelObjectProxy* proxy, ae::ObjId object_id,
                                 std::int32_t width, std::int32_t height) {
  (void)proxy;
  {
    std::lock_guard<std::mutex> lock{mu_};
    assert(post_);
    auto const key = object_id.id();
    auto& slot = slots_[key];
    slot.width = width;
    slot.height = height;
  }
  EnsureDrainScheduled();
}

void PostPresentationSize(ModelObjectProxy* proxy, ae::ObjId object_id,
                          std::int32_t width, std::int32_t height) {
  assert(proxy != nullptr);
  if (PresentationSizeQueue* queue = proxy->GetPresentationSizeQueue()) {
    queue->Post(proxy, object_id, width, height);
  }
}

}  // namespace apptraverse
