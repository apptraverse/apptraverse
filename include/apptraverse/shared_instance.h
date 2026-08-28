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

inline constexpr std::size_t kSharedEventPipelineWindow = 32;

enum class SharedWriteState : std::uint8_t {
  Queued = 0,
  WriteStarted = 1,
  WriteSucceeded = 2,
  WriteFailed = 3,
};

struct PeerInFlightEntry {
  SharedEventId id;
  std::chrono::steady_clock::time_point first_sent_at{};
  std::chrono::steady_clock::time_point last_sent_at{};
  std::uint32_t attempt_count{0};
  SharedWriteState write_state{SharedWriteState::Queued};
};

struct PeerDeliveryState {
  std::string remote_aether_uid;
  bool channel_ready{false};
  std::deque<SharedEventId> pending;
  std::vector<PeerInFlightEntry> in_flight;

  bool HasOutstanding() const noexcept {
    return !pending.empty() || !in_flight.empty();
  }

  bool IsInFlight(SharedEventId const& id) const noexcept {
    for (auto const& entry : in_flight) {
      if (entry.id == id) {
        return true;
      }
    }
    return false;
  }

  void RemoveInFlight(SharedEventId const& id) {
    auto it = in_flight.begin();
    while (it != in_flight.end()) {
      if (it->id == id) {
        it = in_flight.erase(it);
      } else {
        ++it;
      }
    }
  }
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
