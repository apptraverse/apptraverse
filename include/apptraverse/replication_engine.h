#ifndef APPTRAVERSE_REPLICATION_ENGINE_H_
#define APPTRAVERSE_REPLICATION_ENGINE_H_

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_graph_packager.h"
#include "apptraverse/event_record.h"
#include "apptraverse/node.h"
#include "apptraverse/replication_message.h"
#include "apptraverse/replication_state.h"
#include "apptraverse/replication_transport.h"

namespace apptraverse {

class ReplicationEngine : public ReplicationMessageReceiver {
 public:
  ReplicationEngine(Node::ptr root, ReplicationState::ptr state,
                    ae::Domain& message_domain,
                    IReplicationTransport& transport)
      : root_{std::move(root)},
        state_{std::move(state)},
        message_domain_{&message_domain},
        transport_{&transport} {
    assert(root_.is_valid());
    assert(root_.is_loaded());
    assert(state_.is_valid());
    assert(state_.is_loaded());
    assert(message_domain_ != nullptr);
    assert(transport_ != nullptr);
    assert(state_->local_replica_id.IsValid());
    state_->RegisterShared(root_.id());
    if (root_->base.is_valid()) {
      state_->RegisterShared(root_->base.id());
    }
  }

  ReplicaId local_replica_id() const { return state_->local_replica_id; }

  Node::ptr const& root() const { return root_; }

  ReplicationState::ptr const& state() const { return state_; }

  void AddPeer(ReplicaId peer) {
    assert(state_.is_loaded());
    state_->AddPeer(peer);
  }

  void CommitLocal(Event::ptr event) {
    assert(root_.is_loaded());
    assert(state_.is_loaded());
    assert(event.is_valid());
    assert(event.is_loaded());
    assert(event.domain() == root_.domain());

    auto const sequence = state_->AllocateOriginSequence();
    auto const logical_time = state_->TickLamport();

    EventIdentity identity{
        state_->local_replica_id,
        sequence,
    };
    EventOrder order{
        logical_time,
        state_->local_replica_id,
        sequence,
    };

    auto known_before = state_->known_shared_ids;

    EventRecord record{
        identity,
        order,
        event,
    };

    bool const inserted = root_->AcceptSharedEvent(std::move(record));
    assert(inserted);

    state_->origin_packaging.push_back(OriginPackagingSnapshot{
        identity,
        known_before,
    });

    auto const peers_snapshot = state_->known_peers;
    if (peers_snapshot.empty()) {
      state_->MarkGloballyConfirmed(identity);
    } else {
      for (auto const peer : peers_snapshot) {
        assert(state_->FindOutgoing(identity, peer) == nullptr);
        state_->outgoing.push_back(OutgoingDelivery{
            identity,
            peer,
            false,
        });
      }
    }

    FlushOutgoing();
    detail::EventGraphPackager::RegisterIntroducedObjects(*event, *state_);
    TryCollapse();
  }

  void FlushOutgoing() {
    assert(state_.is_loaded());
    assert(root_.is_loaded());

    for (auto& delivery : state_->outgoing) {
      if (delivery.acknowledged) {
        continue;
      }
      SendEventTo(delivery.recipient, delivery.event_identity);
    }
  }

  void SendBootstrap(ReplicaId new_replica) {
    assert(state_.is_loaded());
    assert(root_.is_loaded());
    assert(new_replica.IsValid());
    assert(new_replica != state_->local_replica_id);

    state_->AddPeer(new_replica);

    auto message = BootstrapReplicationMessage::ptr::Create(
        ae::CreateWith{*message_domain_});
    message->root = root_;
    message->root.SetFlags(ae::ObjFlags::kUnloadedByDefault);
    message->globally_confirmed = state_->globally_confirmed;
    message->known_shared_ids = state_->known_shared_ids;
    message->lamport_clock = state_->lamport_clock;

    assert(message->root.is_valid());
    assert(message->root.is_loaded());

    transport_->Send(new_replica, message);
  }

  void TryCollapse() {
    assert(root_.is_loaded());
    assert(state_.is_loaded());

    std::size_t prefix = 0;
    for (auto const& record : root_->journal) {
      if (!state_->IsGloballyConfirmed(record.identity)) {
        break;
      }
      ++prefix;
    }

    if (prefix == 0) {
      return;
    }

    std::vector<EventIdentity> collapsed;
    collapsed.reserve(prefix);
    for (std::size_t i = 0; i < prefix; ++i) {
      collapsed.push_back(root_->journal[i].identity);
    }

    root_->CollapseSharedPrefix(prefix);
    CleanupCollapsedMetadata(collapsed);
  }

  void ReceiveEvent(EventReplicationMessage& message) override {
    assert(root_.is_loaded());
    assert(state_.is_loaded());
    assert(message.identity.IsValid());
    assert(message.order.IsValid());
    assert(message.target.is_valid());
    assert(message.event.is_valid());

    message.target.Load();
    assert(message.target.is_loaded());
    assert(message.target.id() == root_.id());

    message.event.Load();
    assert(message.event.is_loaded());

    state_->TickLamport(message.order.logical_time);

    bool const duplicate = root_->ContainsEvent(message.identity);
    if (!duplicate) {
      EventRecord record{
          message.identity,
          message.order,
          message.event,
      };
      bool const inserted = root_->AcceptSharedEvent(std::move(record));
      assert(inserted);
      detail::EventGraphPackager::RegisterIntroducedObjects(*message.event,
                                                            *state_);
    }

    SendAck(message.identity.origin_replica, message.identity);
    TryCollapse();
  }

  void ReceiveAck(AckReplicationMessage& message) override {
    assert(state_.is_loaded());
    assert(message.identity.IsValid());
    assert(message.from_replica.IsValid());

    auto* delivery =
        state_->FindOutgoing(message.identity, message.from_replica);
    if (delivery != nullptr) {
      delivery->acknowledged = true;
    }

    if (message.identity.origin_replica == state_->local_replica_id &&
        state_->AllRecipientsAcknowledged(message.identity) &&
        !state_->IsGloballyConfirmed(message.identity)) {
      state_->MarkGloballyConfirmed(message.identity);
      BroadcastConfirmed(message.identity);
      TryCollapse();
    }
  }

  void ReceiveConfirmed(ConfirmedReplicationMessage& message) override {
    assert(state_.is_loaded());
    assert(message.identity.IsValid());
    state_->MarkGloballyConfirmed(message.identity);
    TryCollapse();
  }

  void ReceiveBootstrap(BootstrapReplicationMessage& message) override {
    assert(state_.is_loaded());
    assert(message.root.is_valid());
    assert(message.root.id() == root_.id());
    assert(root_.is_loaded());
    assert(root_.domain() != nullptr);

    root_->ReloadFromStorage();
    assert(root_.is_loaded());
    if (root_->base.is_valid()) {
      root_->base.Load();
    }
    for (auto& record : root_->journal) {
      if (record.event.is_valid()) {
        record.event.Load();
      }
    }

    state_->globally_confirmed = message.globally_confirmed;
    state_->known_shared_ids = message.known_shared_ids;
    if (message.lamport_clock > state_->lamport_clock) {
      state_->lamport_clock = message.lamport_clock;
    }

    for (auto& record : root_->journal) {
      assert(record.order.IsValid());
      if (record.order.logical_time > state_->lamport_clock) {
        state_->lamport_clock = record.order.logical_time;
      }
      if (record.event.is_valid() && record.event.is_loaded()) {
        detail::EventGraphPackager::RegisterIntroducedObjects(*record.event,
                                                              *state_);
      }
    }

    state_->RegisterShared(root_.id());
    if (root_->base.is_valid()) {
      state_->RegisterShared(root_->base.id());
    }
  }

 private:
  EventRecord const* FindLocalRecord(EventIdentity const& identity) const {
    return root_->FindRecord(identity);
  }

  std::vector<ae::ObjId> PackagingKnownFor(
      EventIdentity const& identity) const {
    std::vector<ae::ObjId> known;
    if (auto const* packaging = state_->FindOriginPackaging(identity);
        packaging != nullptr) {
      known = packaging->known_shared_before;
    } else {
      known = state_->known_shared_ids;
    }
    if (!detail::EventGraphPackager::ContainsId(known, root_.id())) {
      known.push_back(root_.id());
    }
    if (root_->base.is_valid() &&
        !detail::EventGraphPackager::ContainsId(known, root_->base.id())) {
      known.push_back(root_->base.id());
    }
    return known;
  }

  void SendEventTo(ReplicaId recipient, EventIdentity const& identity) {
    auto const* record = FindLocalRecord(identity);
    assert(record != nullptr);
    assert(record->event.is_valid());
    assert(record->event.is_loaded());

    auto message =
        EventReplicationMessage::ptr::Create(ae::CreateWith{*message_domain_});
    message->identity = record->identity;
    message->order = record->order;
    message->target = root_;
    message->target.Reset();
    message->target.SetFlags(ae::ObjFlags::kUnloadedByDefault);
    message->event = record->event;
    message->event.SetFlags(ae::ObjFlags::kUnloadedByDefault);

    auto packaging_known = PackagingKnownFor(identity);
    detail::EventGraphPackager packager{packaging_known};
    auto restores = packager.UnloadKnownReferences(*message->event);
    transport_->Send(recipient, message);
    detail::EventGraphPackager::Restore(restores);
  }

  void SendAck(ReplicaId origin, EventIdentity const& identity) {
    auto message =
        AckReplicationMessage::ptr::Create(ae::CreateWith{*message_domain_});
    message->identity = identity;
    message->from_replica = state_->local_replica_id;
    transport_->Send(origin, message);
  }

  void BroadcastConfirmed(EventIdentity const& identity) {
    for (auto const peer : state_->known_peers) {
      auto message = ConfirmedReplicationMessage::ptr::Create(
          ae::CreateWith{*message_domain_});
      message->identity = identity;
      transport_->Send(peer, message);
    }
  }

  void CleanupCollapsedMetadata(std::vector<EventIdentity> const& collapsed) {
    auto should_drop = [&](EventIdentity const& identity) {
      return std::find(collapsed.begin(), collapsed.end(), identity) !=
             collapsed.end();
    };

    state_->outgoing.erase(
        std::remove_if(state_->outgoing.begin(), state_->outgoing.end(),
                       [&](OutgoingDelivery const& delivery) {
                         return should_drop(delivery.event_identity);
                       }),
        state_->outgoing.end());

    state_->origin_packaging.erase(
        std::remove_if(state_->origin_packaging.begin(),
                       state_->origin_packaging.end(),
                       [&](OriginPackagingSnapshot const& snapshot) {
                         return should_drop(snapshot.identity);
                       }),
        state_->origin_packaging.end());

    state_->globally_confirmed.erase(
        std::remove_if(state_->globally_confirmed.begin(),
                       state_->globally_confirmed.end(), should_drop),
        state_->globally_confirmed.end());
  }

  Node::ptr root_;
  ReplicationState::ptr state_;
  ae::Domain* message_domain_;
  IReplicationTransport* transport_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_REPLICATION_ENGINE_H_
