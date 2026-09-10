#ifndef APPTRAVERSE_PUBLICATION_CHANNEL_H_
#define APPTRAVERSE_PUBLICATION_CHANNEL_H_

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace apptraverse {

struct ByteSink {
  std::vector<std::uint8_t> bytes;

  void write(void const* data, std::size_t size) {
    auto const* src = static_cast<std::uint8_t const*>(data);
    bytes.insert(bytes.end(), src, src + size);
  }

  void clear_keep_capacity() { bytes.clear(); }
};

struct ByteSource {
  std::uint8_t const* data{nullptr};
  std::size_t size{0};
  std::size_t pos{0};
  bool ok{true};

  void read(void* out, std::size_t n) {
    if (!ok || pos + n > size) {
      ok = false;
      std::memset(out, 0, n);
      return;
    }
    std::memcpy(out, data + pos, n);
    pos += n;
  }
};

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

  // True while a snapshot is waiting for GUI or currently held by the consumer.
  // Model work must not wait on this; only publication into the channel may.
  bool is_publication_busy() const {
    return published_.load(std::memory_order_acquire) >= 0 ||
           in_ui_.load(std::memory_order_acquire) >= 0;
  }

  void PublishProducer() {
    int expected = -1;
    bool const stored = published_.compare_exchange_strong(
        expected, producer_, std::memory_order_release,
        std::memory_order_acquire);
    assert(stored && "unread publication must not be overwritten");
  }

  // Mark the published slot as in-UI *before* clearing published_. Otherwise a
  // concurrent AcquireProducer can see both indices as -1 and clear the buffer
  // the consumer is about to read.
  PublicationBuffer* TakePublished() {
    int const idx = published_.load(std::memory_order_acquire);
    if (idx < 0) {
      return nullptr;
    }
    int expected_in_ui = -1;
    if (!in_ui_.compare_exchange_strong(expected_in_ui, idx,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
      return nullptr;
    }
    int expected_published = idx;
    if (!published_.compare_exchange_strong(expected_published, -1,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
      in_ui_.store(-1, std::memory_order_release);
      return nullptr;
    }
    return &buffers_[static_cast<std::size_t>(idx)];
  }

  // Copy bytes and release the slot so the producer may reuse it immediately.
  std::vector<std::uint8_t> TakePublishedCopy() {
    PublicationBuffer* buffer = TakePublished();
    if (buffer == nullptr) {
      return {};
    }
    std::vector<std::uint8_t> copy = buffer->sink.bytes;
    ReleaseConsumer();
    return copy;
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

}  // namespace apptraverse

#endif  // APPTRAVERSE_PUBLICATION_CHANNEL_H_
