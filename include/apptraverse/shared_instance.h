#ifndef APPTRAVERSE_SHARED_INSTANCE_H_
#define APPTRAVERSE_SHARED_INSTANCE_H_

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "apptraverse/event_record.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_event_order.h"

namespace apptraverse {

struct PeerDeliveryState {
  std::string remote_aether_uid;
  std::deque<SharedEventId> pending;
  std::optional<SharedEventId> in_flight;
  std::chrono::steady_clock::time_point in_flight_sent_at{};
  bool online{false};
  bool channel_ready{false};
};

// Held until CanApply succeeds. Not in known/applied set; not ACK'd.
struct DeferredIncomingEvent {
  SharedEventId id;
  SharedEventOrder order;
  std::vector<std::uint8_t> payload;
  std::string source_peer_uid;
};

template <typename TNode>
struct SharedInstance {
  typename TNode::ptr node;
  std::string shared_room_id;
  std::string local_aether_uid;
  std::uint64_t next_origin_sequence{1};
  std::uint64_t lamport_clock{0};
  std::vector<PeerDeliveryState> peers;
  std::vector<DeferredIncomingEvent> deferred;

  bool HasSharedEvent(SharedEventId const& id) const noexcept {
    return known_events_.count(MakeKey(id)) > 0;
  }

  void RememberSharedEvent(SharedEventId const& id) {
    known_events_.insert(MakeKey(id));
  }

  EventRecord const* FindJournalRecord(SharedEventId const& id) const {
    if (!node.is_valid()) {
      return nullptr;
    }
    for (auto const& record : node->journal) {
      if (record.HasSharedIdentity() && record.identity == id) {
        return &record;
      }
    }
    return nullptr;
  }

  PeerDeliveryState* FindPeer(std::string const& remote_uid) noexcept {
    for (auto& peer : peers) {
      if (peer.remote_aether_uid == remote_uid) {
        return &peer;
      }
    }
    return nullptr;
  }

  PeerDeliveryState const* FindPeer(std::string const& remote_uid) const
      noexcept {
    for (auto const& peer : peers) {
      if (peer.remote_aether_uid == remote_uid) {
        return &peer;
      }
    }
    return nullptr;
  }

 private:
  static std::string MakeKey(SharedEventId const& id) {
    return id.origin_uid + ":" + std::to_string(id.origin_sequence);
  }

  std::unordered_set<std::string> known_events_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_INSTANCE_H_
