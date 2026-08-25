#ifndef APPTRAVERSE_UI_PUBLICATION_H_
#define APPTRAVERSE_UI_PUBLICATION_H_

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "apptraverse/ui_subgraph.h"
#include "demo_model.h"
#include "immutable_object_store.h"
#include "ui_runtime_registry.h"

namespace apptraverse {

struct PublicationBuffer {
  ByteSink sink;
};

template <std::size_t N = 3>
class PublicationChannel {
 public:
  PublicationBuffer* AcquireProducer() {
    int const published = published_.load(std::memory_order_acquire);
    int const in_ui = in_ui_.load(std::memory_order_acquire);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      if (i != published && i != in_ui) {
        producer_ = i;
        buffers_[static_cast<std::size_t>(i)].sink.clear_keep_capacity();
        return &buffers_[static_cast<std::size_t>(i)];
      }
    }
    assert(false && "publication channel exhausted");
    return nullptr;
  }

  bool has_unread_published() const {
    return published_.load(std::memory_order_acquire) >= 0;
  }

  void PublishProducer() {
    int expected = -1;
    bool const stored = published_.compare_exchange_strong(
        expected, producer_, std::memory_order_release,
        std::memory_order_acquire);
    assert(stored && "unread publication must not be overwritten");
  }

  PublicationBuffer* TakePublished() {
    int const idx = published_.exchange(-1, std::memory_order_acq_rel);
    if (idx < 0) {
      return nullptr;
    }
    in_ui_.store(idx, std::memory_order_release);
    return &buffers_[static_cast<std::size_t>(idx)];
  }

  void ReleaseConsumer() { in_ui_.store(-1, std::memory_order_release); }

  std::uint64_t publish_count() const {
    return publish_count_.load(std::memory_order_relaxed);
  }

  void NotePublished() {
    publish_count_.fetch_add(1, std::memory_order_relaxed);
  }

 private:
  std::array<PublicationBuffer, N> buffers_{};
  std::atomic<int> published_{-1};
  std::atomic<int> in_ui_{-1};
  std::atomic<std::uint64_t> publish_count_{0};
  int producer_{0};
};

struct UiApplyResult {
  std::uint32_t root_id{0};
  std::vector<std::uint32_t> changed_obj_ids;
  std::vector<std::uint32_t> reused_obj_ids;
  std::vector<std::uint32_t> const_ref_ids;
};

void SerializeWindowRoot(Window& root, ImmutableObjectStore const& store,
                         std::unordered_map<std::uint32_t, std::uint64_t>&
                             last_published_generation,
                         ByteSink& out);

UiApplyResult DeserializeUiSubgraphIntoExisting(
    ByteSink const& buffer, UiRuntimeRegistry& registry,
    ImmutableObjectStore const& store);

}  // namespace apptraverse

#endif  // APPTRAVERSE_UI_PUBLICATION_H_
