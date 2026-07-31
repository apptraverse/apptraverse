#ifndef APPTRAVERSE_REPLICATION_ENGINE_H_
#define APPTRAVERSE_REPLICATION_ENGINE_H_

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"
#include "aether/obj/obj_id.h"

#include "apptraverse/event_graph_packager.h"
#include "apptraverse/event_record.h"
#include "apptraverse/node.h"
#include "apptraverse/object_graph_traversal.h"
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
    DiscoverSharedGraph();
  }

  ReplicaId local_replica_id() const { return state_->local_replica_id; }

  Node::ptr const& root() const { return root_; }

  ReplicationState::ptr const& state() const { return state_; }

  void AddPeer(ReplicaId peer) {
    assert(state_.is_loaded());
    state_->AddPeer(peer);
  }

  void CommitLocal(Node::ptr target, Event::ptr event) {
    assert(root_.is_loaded());
    assert(state_.is_loaded());
    assert(target.is_valid());
    assert(target.is_loaded());
    assert(target.domain() == root_.domain());
    assert(state_->IsKnownSharedNode(target.id()));
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

    bool const inserted = target->AcceptSharedEvent(std::move(record));
    assert(inserted);

    state_->origin_events.push_back(OriginEventState{
        identity,
        target.id(),
        known_before,
    });

    auto const peers_snapshot = state_->known_peers;
    if (peers_snapshot.empty()) {
      state_->MarkGloballyConfirmed(identity);
      TryCollapse();
    } else {
      for (auto const peer : peers_snapshot) {
        assert(state_->FindOutgoing(identity, peer) == nullptr);
        state_->outgoing.push_back(OutgoingDelivery{
            identity,
            peer,
            false,
        });
      }
      FlushPending();
    }

    detail::EventGraphPackager::RegisterIntroducedObjects(*event, *state_);
    DiscoverSharedGraph();
  }

  void FlushPending() {
    assert(state_.is_loaded());
    assert(root_.is_loaded());
    FlushOutgoingEvents();
    FlushOutgoingConfirmations();
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
    message->known_shared_node_ids = state_->known_shared_node_ids;
    message->lamport_clock = state_->lamport_clock;

    assert(message->root.is_valid());
    assert(message->root.is_loaded());

    transport_->Send(new_replica, message);
  }

  void TryCollapse() {
    assert(root_.is_loaded());
    assert(state_.is_loaded());

    auto const node_ids = state_->known_shared_node_ids;
    std::vector<EventIdentity> collapsed;
    for (auto const node_id : node_ids) {
      Node::ptr node = ResolveSharedNode(node_id);
      if (!node.is_valid() || !node.is_loaded()) {
        continue;
      }

      std::size_t prefix = 0;
      for (auto const& record : node->journal) {
        if (!state_->IsGloballyConfirmed(record.identity)) {
          break;
        }
        ++prefix;
      }

      if (prefix == 0) {
        continue;
      }

      for (std::size_t i = 0; i < prefix; ++i) {
        collapsed.push_back(node->journal[i].identity);
      }
      node->CollapseSharedPrefix(prefix);
    }

    CleanupCollapsedMetadata(collapsed);
  }

  void ReceiveEvent(EventReplicationMessage& message) override {
    assert(root_.is_loaded());
    assert(state_.is_loaded());
    assert(message.identity.IsValid());
    assert(message.order.IsValid());
    assert(message.target.is_valid());
    assert(message.event.is_valid());
    assert(state_->IsKnownSharedNode(message.target.id()));

    Node::ptr target = ResolveSharedNode(message.target.id());
    assert(target.is_valid());
    assert(target.is_loaded());

    message.event.Load();
    assert(message.event.is_loaded());

    state_->TickLamport(message.order.logical_time);

    bool const duplicate = target->ContainsEvent(message.identity);
    if (!duplicate) {
      EventRecord record{
          message.identity,
          message.order,
          message.event,
      };
      bool const inserted = target->AcceptSharedEvent(std::move(record));
      assert(inserted);
      detail::EventGraphPackager::RegisterIntroducedObjects(*message.event,
                                                            *state_);
      DiscoverSharedGraph();
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
      PromoteToGlobalConfirmation(message.identity);
      FlushPending();
      TryCollapse();
    }
  }

  void ReceiveConfirmed(ConfirmedReplicationMessage& message) override {
    assert(state_.is_loaded());
    assert(message.identity.IsValid());
    state_->MarkGloballyConfirmed(message.identity);
    TryCollapse();
    SendConfirmedAck(message.identity.origin_replica, message.identity);
  }

  void ReceiveConfirmedAck(ConfirmedAckReplicationMessage& message) override {
    assert(state_.is_loaded());
    assert(message.identity.IsValid());
    assert(message.from_replica.IsValid());

    auto* delivery =
        state_->FindConfirmation(message.identity, message.from_replica);
    if (delivery != nullptr) {
      delivery->acknowledged = true;
    }

    if (state_->AllConfirmationsAcknowledged(message.identity)) {
      RemoveConfirmationOutbox(message.identity);
      RemoveOriginEventIfIdle(message.identity);
    }
  }

  void ReceiveBootstrap(BootstrapReplicationMessage& message) override {
    assert(state_.is_loaded());
    assert(message.root.is_valid());
    assert(message.root.id() == root_.id());
    assert(root_.is_loaded());
    assert(root_.domain() != nullptr);

    state_->globally_confirmed = message.globally_confirmed;
    state_->known_shared_ids = message.known_shared_ids;
    state_->known_shared_node_ids = message.known_shared_node_ids;
    if (message.lamport_clock > state_->lamport_clock) {
      state_->lamport_clock = message.lamport_clock;
    }

    auto node_ids = state_->known_shared_node_ids;
    if (!state_->IsKnownSharedNode(root_.id())) {
      node_ids.push_back(root_.id());
    }
    for (auto const id : node_ids) {
      Node::ptr node =
          Node::ptr::Declare(ae::CreateWith{*root_.domain()}.with_id(id));
      node.Load();
      assert(node.is_valid());
      assert(node.is_loaded());
      node->ReloadFromStorage();
    }

    root_.Load();
    assert(root_.is_loaded());
    RegisterJournalsFromGraph(*root_);
    DiscoverSharedGraph();
  }

 private:
  struct DeliveryKey {
    EventIdentity identity;
    ReplicaId recipient;
  };

  void DiscoverSharedGraph() {
    assert(root_.is_loaded());

    std::vector<ae::ObjId> candidate_nodes;
    std::vector<ae::ObjId> base_ids;

    class Discovery final : public detail::ObjectGraphTraversal {
     public:
      Discovery(ReplicationState& state, std::vector<ae::ObjId>& candidates,
                std::vector<ae::ObjId>& bases)
          : state_{&state}, candidates_{&candidates}, bases_{&bases} {}

      void OnNode(Node& node) override {
        assert(node.obj_id.IsValid());
        state_->RegisterShared(node.obj_id);
        candidates_->push_back(node.obj_id);
        if (node.base.is_valid()) {
          state_->RegisterShared(node.base.id());
          bases_->push_back(node.base.id());
        }
      }

     private:
      ReplicationState* state_;
      std::vector<ae::ObjId>* candidates_;
      std::vector<ae::ObjId>* bases_;
    };

    Discovery discovery{*state_, candidate_nodes, base_ids};
    root_->TraverseSharedGraph(discovery);

    for (auto const id : candidate_nodes) {
      bool is_base = false;
      for (auto const base_id : base_ids) {
        if (base_id == id) {
          is_base = true;
          break;
        }
      }
      if (!is_base) {
        state_->RegisterSharedNode(id);
      }
    }
  }

  void RegisterJournalsFromGraph(Node& node) {
    class JournalRegistrar final : public detail::ObjectGraphTraversal {
     public:
      explicit JournalRegistrar(ReplicationState& state) : state_{&state} {}

      void OnNode(Node& current) override {
        for (auto& record : current.journal) {
          assert(record.order.IsValid());
          if (record.order.logical_time > state_->lamport_clock) {
            state_->lamport_clock = record.order.logical_time;
          }
          if (record.event.is_valid() && record.event.is_loaded()) {
            detail::EventGraphPackager::RegisterIntroducedObjects(*record.event,
                                                                  *state_);
          }
        }
      }

     private:
      ReplicationState* state_;
    };

    JournalRegistrar registrar{*state_};
    registrar.Traverse(node);
  }

  Node::ptr ResolveSharedNode(ae::ObjId id) const {
    assert(id.IsValid());
    assert(state_->IsKnownSharedNode(id));
    assert(root_.domain() != nullptr);

    Node::ptr node =
        Node::ptr::Declare(ae::CreateWith{*root_.domain()}.with_id(id));
    node.Load();
    assert(node.is_valid());
    assert(node.is_loaded());
    assert(node.Load().as<Node>() != nullptr);
    return node;
  }

  EventRecord const* FindLocalRecord(EventIdentity const& identity,
                                     ae::ObjId target_node) const {
    Node::ptr node = ResolveSharedNode(target_node);
    return node->FindRecord(identity);
  }

  std::vector<ae::ObjId> PackagingKnownFor(
      EventIdentity const& identity) const {
    std::vector<ae::ObjId> known;
    if (auto const* origin = state_->FindOriginEvent(identity);
        origin != nullptr) {
      known = origin->known_shared_before;
    } else {
      known = state_->known_shared_ids;
    }
    for (auto const node_id : state_->known_shared_node_ids) {
      if (!detail::EventGraphPackager::ContainsId(known, node_id)) {
        known.push_back(node_id);
      }
    }
    if (root_->base.is_valid() &&
        !detail::EventGraphPackager::ContainsId(known, root_->base.id())) {
      known.push_back(root_->base.id());
    }
    return known;
  }

  void FlushOutgoingEvents() {
    std::vector<DeliveryKey> snapshot;
    snapshot.reserve(state_->outgoing.size());
    for (auto const& delivery : state_->outgoing) {
      if (!delivery.acknowledged) {
        snapshot.push_back(DeliveryKey{delivery.event_identity,
                                       delivery.recipient});
      }
    }

    for (auto const& key : snapshot) {
      auto* delivery = state_->FindOutgoing(key.identity, key.recipient);
      if (delivery == nullptr || delivery->acknowledged) {
        continue;
      }
      SendEventTo(key.recipient, key.identity);
    }
  }

  void FlushOutgoingConfirmations() {
    std::vector<DeliveryKey> snapshot;
    snapshot.reserve(state_->confirmation_outgoing.size());
    for (auto const& delivery : state_->confirmation_outgoing) {
      if (!delivery.acknowledged) {
        snapshot.push_back(
            DeliveryKey{delivery.identity, delivery.recipient});
      }
    }

    for (auto const& key : snapshot) {
      auto* delivery = state_->FindConfirmation(key.identity, key.recipient);
      if (delivery == nullptr || delivery->acknowledged) {
        continue;
      }
      SendConfirmationTo(key.recipient, key.identity);
    }
  }

  void SendEventTo(ReplicaId recipient, EventIdentity const& identity) {
    auto const* origin = state_->FindOriginEvent(identity);
    assert(origin != nullptr);
    assert(origin->target_node.IsValid());

    auto const* record = FindLocalRecord(identity, origin->target_node);
    assert(record != nullptr);
    assert(record->event.is_valid());
    assert(record->event.is_loaded());

    Node::ptr target = ResolveSharedNode(origin->target_node);

    auto message =
        EventReplicationMessage::ptr::Create(ae::CreateWith{*message_domain_});
    message->identity = record->identity;
    message->order = record->order;
    message->target = target;
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

  void PromoteToGlobalConfirmation(EventIdentity const& identity) {
    state_->MarkGloballyConfirmed(identity);

    std::vector<ReplicaId> recipients;
    for (auto const& delivery : state_->outgoing) {
      if (delivery.event_identity == identity) {
        recipients.push_back(delivery.recipient);
      }
    }

    state_->outgoing.erase(
        std::remove_if(state_->outgoing.begin(), state_->outgoing.end(),
                       [&](OutgoingDelivery const& delivery) {
                         return delivery.event_identity == identity;
                       }),
        state_->outgoing.end());

    for (auto const recipient : recipients) {
      assert(state_->FindConfirmation(identity, recipient) == nullptr);
      state_->confirmation_outgoing.push_back(ConfirmationDelivery{
          identity,
          recipient,
          false,
      });
    }
  }

  void SendConfirmationTo(ReplicaId recipient, EventIdentity const& identity) {
    auto message = ConfirmedReplicationMessage::ptr::Create(
        ae::CreateWith{*message_domain_});
    message->identity = identity;
    transport_->Send(recipient, message);
  }

  void SendConfirmedAck(ReplicaId origin, EventIdentity const& identity) {
    auto message = ConfirmedAckReplicationMessage::ptr::Create(
        ae::CreateWith{*message_domain_});
    message->identity = identity;
    message->from_replica = state_->local_replica_id;
    transport_->Send(origin, message);
  }

  void RemoveConfirmationOutbox(EventIdentity const& identity) {
    state_->confirmation_outgoing.erase(
        std::remove_if(state_->confirmation_outgoing.begin(),
                       state_->confirmation_outgoing.end(),
                       [&](ConfirmationDelivery const& delivery) {
                         return delivery.identity == identity;
                       }),
        state_->confirmation_outgoing.end());
  }

  void RemoveOriginEventIfIdle(EventIdentity const& identity) {
    if (state_->HasPendingConfirmation(identity)) {
      return;
    }
    for (auto const& delivery : state_->outgoing) {
      if (delivery.event_identity == identity) {
        return;
      }
    }

    state_->origin_events.erase(
        std::remove_if(state_->origin_events.begin(),
                       state_->origin_events.end(),
                       [&](OriginEventState const& entry) {
                         return entry.identity == identity;
                       }),
        state_->origin_events.end());
  }

  void CleanupCollapsedMetadata(std::vector<EventIdentity> const& collapsed) {
    for (auto const& identity : collapsed) {
      // Keep origin/confirmation metadata while reliable confirmations remain.
      if (state_->HasPendingConfirmation(identity)) {
        continue;
      }

      state_->origin_events.erase(
          std::remove_if(state_->origin_events.begin(),
                         state_->origin_events.end(),
                         [&](OriginEventState const& entry) {
                           return entry.identity == identity;
                         }),
          state_->origin_events.end());

      state_->globally_confirmed.erase(
          std::remove(state_->globally_confirmed.begin(),
                      state_->globally_confirmed.end(), identity),
          state_->globally_confirmed.end());
    }
  }

  Node::ptr root_;
  ReplicationState::ptr state_;
  ae::Domain* message_domain_;
  IReplicationTransport* transport_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_REPLICATION_ENGINE_H_
