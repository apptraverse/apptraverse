#ifndef APPTRAVERSE_UNRELIABLE_MEMORY_LINK_H_
#define APPTRAVERSE_UNRELIABLE_MEMORY_LINK_H_

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

#include "apptraverse/shared_graph_sync_session.h"

namespace apptraverse {

// Deterministic in-memory transport between two sessions. No threads/timers —
// the test drives Deliver / Drop / Duplicate / RetryPending explicitly.
class UnreliableMemoryLink {
 public:
  struct Envelope {
    int direction{0};  // 0: left→right, 1: right→left
    SerializedSyncPacket bytes;
  };

  SharedGraphSyncSession::SendFunction MakeSend(int direction) {
    assert(direction == 0 || direction == 1);
    return [this, direction](ae::ObjId /*packet_id*/,
                             SerializedSyncPacket bytes) {
      envelopes_.push_back(Envelope{direction, std::move(bytes)});
    };
  }

  void Bind(SharedGraphSyncSession& left, SharedGraphSyncSession& right) {
    endpoints_[0] = &left;
    endpoints_[1] = &right;
  }

  std::size_t pending_count() const { return envelopes_.size(); }

  std::vector<Envelope> const& envelopes() const { return envelopes_; }

  void Deliver(std::size_t index) {
    assert(index < envelopes_.size());
    assert(endpoints_[0] != nullptr);
    assert(endpoints_[1] != nullptr);
    Envelope env = std::move(envelopes_[index]);
    envelopes_.erase(envelopes_.begin() +
                     static_cast<std::ptrdiff_t>(index));
    int const dest = env.direction == 0 ? 1 : 0;
    endpoints_[dest]->Receive(env.bytes);
  }

  void DeliverNext() {
    assert(!envelopes_.empty());
    Deliver(0);
  }

  void DeliverAllInOrder() {
    while (!envelopes_.empty()) {
      DeliverNext();
    }
  }

  void Drop(std::size_t index) {
    assert(index < envelopes_.size());
    envelopes_.erase(envelopes_.begin() +
                     static_cast<std::ptrdiff_t>(index));
  }

  void Duplicate(std::size_t index) {
    assert(index < envelopes_.size());
    envelopes_.insert(envelopes_.begin() +
                          static_cast<std::ptrdiff_t>(index) + 1,
                      envelopes_[index]);
  }

  void Move(std::size_t from, std::size_t to) {
    assert(from < envelopes_.size());
    assert(to < envelopes_.size());
    if (from == to) {
      return;
    }
    Envelope env = std::move(envelopes_[from]);
    envelopes_.erase(envelopes_.begin() +
                     static_cast<std::ptrdiff_t>(from));
    if (to > from) {
      --to;
    }
    envelopes_.insert(envelopes_.begin() + static_cast<std::ptrdiff_t>(to),
                      std::move(env));
  }

 private:
  SharedGraphSyncSession* endpoints_[2]{nullptr, nullptr};
  std::vector<Envelope> envelopes_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_UNRELIABLE_MEMORY_LINK_H_
