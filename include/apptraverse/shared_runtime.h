#ifndef APPTRAVERSE_SHARED_RUNTIME_H_
#define APPTRAVERSE_SHARED_RUNTIME_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_event_order.h"
#include "apptraverse/shared_instance.h"
#include "apptraverse/shared_transport.h"

namespace apptraverse {

inline constexpr std::chrono::milliseconds kSharedEventRetryInterval{1000};

using SharedLogFn = std::function<void(std::string const& line)>;

struct SharedRuntimeConfig {
  SharedLogFn log;
};

class SharedRuntime {
 public:
  explicit SharedRuntime(SharedRuntimeConfig config = {});

  template <typename TNode>
  SharedEventId AssignLocalIdentity(SharedInstance<TNode>& instance) {
    SharedEventId id{
        .origin_uid = instance.local_aether_uid,
        .origin_sequence = instance.next_origin_sequence++,
    };
    ++instance.lamport_clock;
    return id;
  }

  template <typename TNode>
  SharedEventOrder MakeLocalOrder(SharedInstance<TNode>& instance,
                                  SharedEventId const& id) {
    return SharedEventOrder{
        .lamport = instance.lamport_clock,
        .origin_uid = id.origin_uid,
        .origin_sequence = id.origin_sequence,
    };
  }

  template <typename TNode>
  void RememberLocalCommit(SharedInstance<TNode>& instance,
                           SharedEventId const& id) {
    instance.RememberSharedEvent(id);
  }

  template <typename TNode>
  void OnLocalEventCommitted(
      SharedInstance<TNode>& instance, SharedEventId const& id,
      std::function<void(PeerDeliveryState&, SharedEventId const&)> const&
          enqueue_for_peer);

  template <typename TNode>
  PeerDeliveryState& EnsurePeer(SharedInstance<TNode>& instance,
                                std::string const& remote_uid);

  template <typename TNode>
  void SeedPendingFromJournal(SharedInstance<TNode>& instance,
                              PeerDeliveryState& peer);

  template <typename TNode>
  void OnIncomingEventApplied(
      SharedInstance<TNode>& instance, SharedEventId const& id,
      SharedEventOrder const& order, std::string const& source_peer_uid,
      std::function<void(PeerDeliveryState&, SharedEventId const&)> const&
          enqueue_for_peer);

  template <typename TNode>
  void OnAckReceived(SharedInstance<TNode>& instance,
                     std::string const& from_peer_uid,
                     SharedEventId const& event_id);

  template <typename TNode>
  void Tick(SharedInstance<TNode>& instance,
            std::chrono::steady_clock::time_point now,
            std::function<bool(PeerDeliveryState&, SharedEventId const&)> const&
                try_send);

 private:
  SharedRuntimeConfig config_;
};

template <typename TNode>
void SharedRuntime::OnLocalEventCommitted(
    SharedInstance<TNode>& instance, SharedEventId const& id,
    std::function<void(PeerDeliveryState&, SharedEventId const&)> const&
        enqueue_for_peer) {
  for (auto& peer : instance.peers) {
    if (peer.remote_aether_uid == id.origin_uid) {
      continue;
    }
    enqueue_for_peer(peer, id);
    if (config_.log) {
      config_.log("SHARED_EVENT_PENDING room_id=" + instance.shared_room_id +
                  " peer_uid=" + peer.remote_aether_uid + " event_id=" +
                  id.origin_uid + ":" + std::to_string(id.origin_sequence));
    }
  }
}

template <typename TNode>
PeerDeliveryState& SharedRuntime::EnsurePeer(SharedInstance<TNode>& instance,
                                             std::string const& remote_uid) {
  if (auto* existing = instance.FindPeer(remote_uid)) {
    return *existing;
  }
  PeerDeliveryState peer{.remote_aether_uid = remote_uid};
  instance.peers.push_back(std::move(peer));
  if (config_.log) {
    config_.log("SHARED_PEER_CREATED room_id=" + instance.shared_room_id +
                " peer_uid=" + remote_uid);
  }
  return instance.peers.back();
}

template <typename TNode>
void SharedRuntime::SeedPendingFromJournal(SharedInstance<TNode>& instance,
                                           PeerDeliveryState& peer) {
  if (!instance.node.is_valid()) {
    return;
  }
  for (auto const& record : instance.node->journal) {
    if (!record.HasSharedIdentity()) {
      continue;
    }
    if (record.identity.origin_uid == peer.remote_aether_uid) {
      continue;
    }
    bool already = false;
    for (auto const& pending : peer.pending) {
      if (pending == record.identity) {
        already = true;
        break;
      }
    }
    if (!already && !peer.IsInFlight(record.identity)) {
      peer.pending.push_back(record.identity);
    }
  }
}

template <typename TNode>
void SharedRuntime::OnIncomingEventApplied(
    SharedInstance<TNode>& instance, SharedEventId const& id,
    SharedEventOrder const& order, std::string const& source_peer_uid,
    std::function<void(PeerDeliveryState&, SharedEventId const&)> const&
        enqueue_for_peer) {
  if (instance.HasSharedEvent(id)) {
    if (config_.log) {
      config_.log("SHARED_EVENT_DUPLICATE room_id=" + instance.shared_room_id +
                  " event_id=" + id.origin_uid + ":" +
                  std::to_string(id.origin_sequence));
    }
    return;
  }
  instance.RememberSharedEvent(id);
  if (order.lamport > instance.lamport_clock) {
    instance.lamport_clock = order.lamport;
  }
  if (config_.log) {
    config_.log("SHARED_EVENT_APPLIED room_id=" + instance.shared_room_id +
                " event_id=" + id.origin_uid + ":" +
                std::to_string(id.origin_sequence));
  }
  for (auto& peer : instance.peers) {
    if (peer.remote_aether_uid == source_peer_uid ||
        peer.remote_aether_uid == id.origin_uid) {
      continue;
    }
    enqueue_for_peer(peer, id);
  }
}

template <typename TNode>
void SharedRuntime::OnAckReceived(SharedInstance<TNode>& instance,
                                  std::string const& from_peer_uid,
                                  SharedEventId const& event_id) {
  auto* peer = instance.FindPeer(from_peer_uid);
  if (peer == nullptr) {
    return;
  }
  peer->RemoveInFlight(event_id);
  auto it = peer->pending.begin();
  while (it != peer->pending.end()) {
    if (*it == event_id) {
      it = peer->pending.erase(it);
    } else {
      ++it;
    }
  }
  if (config_.log) {
    config_.log("SHARED_EVENT_DELIVERED room_id=" + instance.shared_room_id +
                " peer_uid=" + from_peer_uid + " event_id=" +
                event_id.origin_uid + ":" +
                std::to_string(event_id.origin_sequence));
  }
}

template <typename TNode>
void SharedRuntime::Tick(
    SharedInstance<TNode>& instance, std::chrono::steady_clock::time_point now,
    std::function<bool(PeerDeliveryState&, SharedEventId const&)> const&
        try_send) {
  for (auto& peer : instance.peers) {
    if (!peer.channel_ready) {
      continue;
    }

    for (auto& entry : peer.in_flight) {
      if (now - entry.last_sent_at < kSharedEventRetryInterval) {
        continue;
      }
      entry.last_sent_at = now;
      ++entry.attempt_count;
      entry.write_state = SharedWriteState::WriteStarted;
      if (try_send(peer, entry.id)) {
        if (config_.log) {
          config_.log("SHARED_EVENT_SEND room_id=" + instance.shared_room_id +
                      " peer_uid=" + peer.remote_aether_uid + " event_id=" +
                      entry.id.origin_uid + ":" +
                      std::to_string(entry.id.origin_sequence) + " retry=1");
        }
      }
    }

    while (!peer.pending.empty() &&
           peer.in_flight.size() < kSharedEventPipelineWindow) {
      SharedEventId const id = peer.pending.front();
      if (peer.IsInFlight(id)) {
        peer.pending.pop_front();
        continue;
      }
      PeerInFlightEntry entry{
          .id = id,
          .first_sent_at = now,
          .last_sent_at = now,
          .attempt_count = 1,
          .write_state = SharedWriteState::WriteStarted,
      };
      if (try_send(peer, id)) {
        peer.in_flight.push_back(entry);
        peer.pending.pop_front();
        if (config_.log) {
          config_.log("SHARED_EVENT_SEND room_id=" + instance.shared_room_id +
                      " peer_uid=" + peer.remote_aether_uid + " event_id=" +
                      id.origin_uid + ":" +
                      std::to_string(id.origin_sequence));
        }
      } else {
        break;
      }
    }
  }
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_RUNTIME_H_
