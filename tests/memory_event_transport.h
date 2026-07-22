#ifndef TESTS_MEMORY_EVENT_TRANSPORT_H_
#define TESTS_MEMORY_EVENT_TRANSPORT_H_

#include <cassert>
#include <utility>
#include <vector>

#include "apptraverse/event_transport.h"

namespace apptraverse::test {

enum class MemoryTransportMode {
  kImmediate,
  kQueued,
};

class MemoryEventTransport final : public IEventTransport {
 public:
  void Connect(MemoryEventTransport& peer) {
    peer_ = &peer;
    peer.peer_ = this;
  }

  void SetMode(MemoryTransportMode mode) { mode_ = mode; }

  void SetReceiver(IEventTransportReceiver* receiver) override {
    receiver_ = receiver;
  }

  void SendEvent(EventTransportMessage message) override {
    assert(peer_ != nullptr);
    if (mode_ == MemoryTransportMode::kImmediate) {
      peer_->DeliverEvent(std::move(message));
      return;
    }
    outbound_events_.push_back(std::move(message));
  }

  void SendConfirmation(EventConfirmation confirmation) override {
    assert(peer_ != nullptr);
    if (mode_ == MemoryTransportMode::kImmediate) {
      peer_->DeliverConfirmation(std::move(confirmation));
      return;
    }
    outbound_confirmations_.push_back(std::move(confirmation));
  }

  void DeliverQueuedEventsInOrder() {
    auto queued = std::move(outbound_events_);
    outbound_events_.clear();
    for (auto& message : queued) {
      assert(peer_ != nullptr);
      peer_->DeliverEvent(std::move(message));
    }
  }

  void DeliverQueuedEventsReversed() {
    auto queued = std::move(outbound_events_);
    outbound_events_.clear();
    for (auto it = queued.rbegin(); it != queued.rend(); ++it) {
      assert(peer_ != nullptr);
      peer_->DeliverEvent(std::move(*it));
    }
  }

  void DeliverQueuedConfirmations() {
    auto queued = std::move(outbound_confirmations_);
    outbound_confirmations_.clear();
    for (auto& confirmation : queued) {
      assert(peer_ != nullptr);
      peer_->DeliverConfirmation(std::move(confirmation));
    }
  }

  void RedeliverLastEvent() {
    assert(!last_delivered_event_.identity.IsValid() ||
           last_delivered_event_.identity.IsValid());
    assert(last_delivered_event_.identity.IsValid());
    DeliverEvent(last_delivered_event_);
  }

  std::size_t queued_event_count() const { return outbound_events_.size(); }

 private:
  void DeliverEvent(EventTransportMessage message) {
    last_delivered_event_ = message;
    assert(receiver_ != nullptr);
    receiver_->OnEvent(std::move(message));
  }

  void DeliverConfirmation(EventConfirmation confirmation) {
    assert(receiver_ != nullptr);
    receiver_->OnConfirmation(std::move(confirmation));
  }

  MemoryEventTransport* peer_{nullptr};
  IEventTransportReceiver* receiver_{nullptr};
  MemoryTransportMode mode_{MemoryTransportMode::kImmediate};
  std::vector<EventTransportMessage> outbound_events_;
  std::vector<EventConfirmation> outbound_confirmations_;
  EventTransportMessage last_delivered_event_{};
};

}  // namespace apptraverse::test

#endif  // TESTS_MEMORY_EVENT_TRANSPORT_H_
