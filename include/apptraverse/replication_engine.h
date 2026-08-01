#ifndef APPTRAVERSE_REPLICATION_ENGINE_H_
#define APPTRAVERSE_REPLICATION_ENGINE_H_

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"
#include "aether/obj/obj_id.h"

#include "apptraverse/event_graph_packager.h"
#include "apptraverse/event_record.h"
#include "apptraverse/node.h"
#include "apptraverse/object_graph_traversal.h"
#include "apptraverse/replication_clock.h"
#include "apptraverse/replication_message.h"
#include "apptraverse/replication_state.h"
#include "apptraverse/replication_transport.h"

namespace apptraverse {

class ReplicationEngine : public ReplicationMessageReceiver {
 public:
  ReplicationEngine(Node::ptr root, ReplicationState::ptr state,
                    ae::Domain& message_domain,
                    IReplicationTransport& transport, IReplicationClock& clock)
      : root_{std::move(root)},
        state_{std::move(state)},
        message_domain_{&message_domain},
        transport_{&transport},
        clock_{&clock} {
    assert(root_.is_valid());
    assert(root_.is_loaded());
    assert(state_.is_valid());
    assert(state_.is_loaded());
    assert(message_domain_ != nullptr);
    assert(transport_ != nullptr);
    assert(clock_ != nullptr);
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

    auto const timestamp_us = clock_->NowMicroseconds();
    assert(timestamp_us != 0);

    auto const sequence = state_->AllocateOriginSequence();

    EventIdentity identity{
        state_->local_replica_id,
        sequence,
    };
    EventOrder order{
        timestamp_us,
    };

    auto known_before = state_->known_shared_ids;

    EventRecord record{
        identity,
        order,
        event,
    };

    bool const inserted = target->AcceptSharedEvent(std::move(record));
    assert(inserted);

    auto const peers_snapshot = state_->known_peers;
    if (!peers_snapshot.empty()) {
      state_->origin_events.push_back(OriginEventState{
          identity,
          target.id(),
          known_before,
      });
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
  }

  // Delivers a bootstrap snapshot. Does not add the recipient as a peer.
  void SendBootstrap(ReplicaId new_replica) {
    assert(state_.is_loaded());
    assert(root_.is_loaded());
    assert(new_replica.IsValid());
    assert(new_replica != state_->local_replica_id);

    auto message = BootstrapReplicationMessage::ptr::Create(
        ae::CreateWith{*message_domain_});
    message->root = root_;
    message->root.SetFlags(ae::ObjFlags::kUnloadedByDefault);
    message->known_shared_ids = state_->known_shared_ids;
    message->known_shared_node_ids = state_->known_shared_node_ids;

    assert(message->root.is_valid());
    assert(message->root.is_loaded());

    message.Save();
    transport_->Send(new_replica, message);
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
        state_->AllRecipientsAcknowledged(message.identity)) {
      RemoveOutgoing(message.identity);
      RemoveOriginEvent(message.identity);
    }

    FlushPending();
  }

  void ReceiveBootstrap(BootstrapReplicationMessage& message) override {
    assert(state_.is_loaded());
    assert(message.root.is_valid());
    assert(message.root.id() == root_.id());
    assert(root_.is_loaded());
    assert(root_.domain() != nullptr);

    state_->known_shared_ids = message.known_shared_ids;
    state_->known_shared_node_ids = message.known_shared_node_ids;

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

  std::uint64_t LookupTimestamp(EventIdentity const& identity) const {
    auto const* origin = state_->FindOriginEvent(identity);
    assert(origin != nullptr);
    auto const* record = FindLocalRecord(identity, origin->target_node);
    assert(record != nullptr);
    assert(record->order.timestamp_us != 0);
    return record->order.timestamp_us;
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
        snapshot.push_back(
            DeliveryKey{delivery.event_identity, delivery.recipient});
      }
    }

    std::vector<ReplicaId> recipients;
    for (auto const& key : snapshot) {
      if (std::find(recipients.begin(), recipients.end(), key.recipient) ==
          recipients.end()) {
        recipients.push_back(key.recipient);
      }
    }

    for (auto const recipient : recipients) {
      EventIdentity earliest{};
      std::uint64_t earliest_ts = std::numeric_limits<std::uint64_t>::max();
      bool have = false;

      for (auto const& key : snapshot) {
        if (key.recipient != recipient) {
          continue;
        }
        auto* delivery = state_->FindOutgoing(key.identity, key.recipient);
        if (delivery == nullptr || delivery->acknowledged) {
          continue;
        }
        auto const ts = LookupTimestamp(key.identity);
        if (!have || ts < earliest_ts) {
          earliest = key.identity;
          earliest_ts = ts;
          have = true;
        }
      }

      if (!have) {
        continue;
      }

      auto* delivery = state_->FindOutgoing(earliest, recipient);
      if (delivery == nullptr || delivery->acknowledged) {
        continue;
      }
      SendEventTo(recipient, earliest);
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

    // Fully serialize while live Event refs are in packaging shape, then
    // restore the live Event graph before any reentrant transport work.
    message.Save();
    detail::EventGraphPackager::Restore(restores);

    transport_->Send(recipient, message);
  }

  void SendAck(ReplicaId origin, EventIdentity const& identity) {
    auto message =
        AckReplicationMessage::ptr::Create(ae::CreateWith{*message_domain_});
    message->identity = identity;
    message->from_replica = state_->local_replica_id;
    message.Save();
    transport_->Send(origin, message);
  }

  void RemoveOutgoing(EventIdentity const& identity) {
    state_->outgoing.erase(
        std::remove_if(state_->outgoing.begin(), state_->outgoing.end(),
                       [&](OutgoingDelivery const& delivery) {
                         return delivery.event_identity == identity;
                       }),
        state_->outgoing.end());
  }

  void RemoveOriginEvent(EventIdentity const& identity) {
    state_->origin_events.erase(
        std::remove_if(state_->origin_events.begin(),
                       state_->origin_events.end(),
                       [&](OriginEventState const& entry) {
                         return entry.identity == identity;
                       }),
        state_->origin_events.end());
  }

  Node::ptr root_;
  ReplicationState::ptr state_;
  ae::Domain* message_domain_;
  IReplicationTransport* transport_;
  IReplicationClock* clock_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_REPLICATION_ENGINE_H_
