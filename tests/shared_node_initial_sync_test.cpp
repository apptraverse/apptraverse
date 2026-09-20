#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/link.h"
#include "apptraverse/memory_transport.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_network_graph.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/shared_sync_runtime.h"
#include "apptraverse/sync_frame.h"

#include "shared_node_demo_model.h"

namespace apptraverse::test {
namespace {

using apptraverse::example::shared_node::SetValueEvent;
using apptraverse::example::shared_node::SharedValueNode;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

std::string const kEndpointA = "replica-a";
std::string const kEndpointB = "replica-b";
std::string const kEndpointC = "replica-c";

// Storage wrapper that records, for every write, how many packets were already
// queued toward the peer. It proves durability ordering: a replica that
// persists before it transmits writes only while that queue is still empty.
class WatchedStorage final : public ae::IDomainStorage {
 public:
  WatchedStorage(ae::IDomainStorage& inner, MemoryNetwork const& network,
                 std::string from, std::string to)
      : inner_{inner},
        network_{network},
        from_{std::move(from)},
        to_{std::move(to)} {}

  void ResetWatch() { pending_at_store_.clear(); }

  std::vector<std::size_t> const& pending_at_store() const {
    return pending_at_store_;
  }

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override {
    pending_at_store_.push_back(network_.PendingCount(from_, to_));
    return inner_.Store(query);
  }

  ae::ClassList Enumerate(ae::ObjId const& obj_id) override {
    return inner_.Enumerate(obj_id);
  }

  ae::DomainLoad Load(ae::DomainQuery const& query) override {
    return inner_.Load(query);
  }

  void Remove(ae::ObjId const& obj_id) override { inner_.Remove(obj_id); }
  void CleanUp() override { inner_.CleanUp(); }

 private:
  ae::IDomainStorage& inner_;
  MemoryNetwork const& network_;
  std::string from_;
  std::string to_;
  std::vector<std::size_t> pending_at_store_;
};

// One replica: its own storage, Domain, transport endpoint, and sync runtime.
// Storage outlives Stop/Start, which destroy and rebuild everything else, so a
// restart recovers from persisted bytes only.
struct Replica {
  Replica(MemoryNetwork& network, std::string endpoint_uid,
          std::string peer_endpoint_uid)
      : watched{storage, network, endpoint_uid, std::move(peer_endpoint_uid)},
        network_{network},
        endpoint_uid_{std::move(endpoint_uid)} {}

  void Start() {
    domain = std::make_unique<ae::Domain>(watched);
    transport = std::make_unique<MemoryTransport>(network_, endpoint_uid_);
    sync = std::make_unique<SharedSyncRuntime>(*domain, watched, *transport);
  }

  // Full restart: everything but the persisted bytes is destroyed. Callers
  // must not hold object pointers of this replica across it.
  void Stop() {
    sync.reset();
    transport.reset();
    domain.reset();
  }

  ae::RamDomainStorage storage;
  WatchedStorage watched;
  std::unique_ptr<ae::Domain> domain;
  std::unique_ptr<MemoryTransport> transport;
  std::unique_ptr<SharedSyncRuntime> sync;

 private:
  MemoryNetwork& network_;
  std::string endpoint_uid_;
};

MemoryLink::ptr MakeMemoryLink(ae::Domain& domain, ae::ObjId id,
                               std::string endpoint) {
  auto link = MemoryLink::ptr::Create(ae::CreateWith{domain}.with_id(id));
  link->endpoint_uid = std::move(endpoint);
  link->heartbeat_interval_ms = 1000;
  InitializeRuntimeNode(*link);
  return link;
}

void SetValue(SharedValueNode& node, std::int32_t value) {
  auto event = SetValueEvent::ptr::Create(ae::CreateWith{*node.domain});
  event->value = value;
  node.Commit(event);
}

struct SenderFixture {
  ae::ObjId node_id;
  ae::ObjId share_to_a;
  ae::ObjId share_to_b;
  ae::ObjId share_to_c;
};

// A owns a SharedNode that already has business history and the Link topology
// of both participants. B is one of the shared endpoints but has no state yet.
// A non-zero link_c_id adds a third participant, for cases that need an
// endpoint which is in the topology but is not the destination.
SenderFixture BuildSharedNode(Replica& a, ae::ObjId::Type node_id,
                              ae::ObjId::Type link_a_id,
                              ae::ObjId::Type link_b_id, std::int32_t value,
                              ae::ObjId::Type link_c_id = 0) {
  auto node =
      SharedValueNode::ptr::Create(ae::CreateWith{*a.domain}.with_id(node_id));
  InitializeRuntimeNode(*node);
  auto link_a = MakeMemoryLink(*a.domain, ae::ObjId{link_a_id}, kEndpointA);
  auto link_b = MakeMemoryLink(*a.domain, ae::ObjId{link_b_id}, kEndpointB);
  node->InstallLocalShare(link_a, ShareAccess::ReadWrite);
  node->InstallLocalShare(link_b, ShareAccess::ReadWrite);
  if (link_c_id != 0) {
    auto link_c = MakeMemoryLink(*a.domain, ae::ObjId{link_c_id}, kEndpointC);
    node->InstallLocalShare(link_c, ShareAccess::ReadWrite);
    link_c.Save();
  }
  SetValue(*node, value);
  node.Save();
  link_a.Save();
  link_b.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }

  SenderFixture fixture{
      .node_id = node.id(),
      .share_to_a = node->shares[0].share_id,
      .share_to_b = node->shares[1].share_id,
      .share_to_c = link_c_id != 0 ? node->shares[2].share_id : ae::ObjId{},
  };
  a.sync->RegisterNode(node);
  return fixture;
}

// A bare endpoint on the network: it can send crafted bytes and counts what it
// receives, with no Domain or sync runtime behind it.
struct ObserverEndpoint {
  ObserverEndpoint(MemoryNetwork& network, std::string endpoint_uid)
      : transport{network, std::move(endpoint_uid)} {
    transport.BindReceive(this, &ObserverEndpoint::Thunk);
  }

  static void Thunk(void* ctx, std::string const&,
                    std::vector<std::uint8_t> const&) {
    ++static_cast<ObserverEndpoint*>(ctx)->received;
  }

  MemoryTransport transport;
  std::size_t received = 0;
};

void ReloadAndRegister(Replica& replica, ae::ObjId node_id) {
  auto node =
      SharedValueNode::ptr::Declare(ae::CreateWith{*replica.domain}.with_id(
          node_id));
  node.Load();
  CHECK(node.is_loaded());
  replica.sync->RegisterNode(node);
}

LinkSyncState::ptr SyncStateOf(SharedNode::ptr node, ae::ObjId share_id) {
  auto const index = node->FindLinkSyncIndexForShare(share_id);
  CHECK(index < node->link_sync_states.size());
  auto state = node->link_sync_states[index];
  state.Load();
  return state;
}

std::int32_t ValueOf(SharedNode::ptr node) {
  SharedValueNode::ptr concrete = node;
  concrete.Load();
  CHECK(concrete.is_loaded());
  return concrete->value;
}

void AllStoresBeforeAnySend(WatchedStorage const& storage) {
  CHECK(!storage.pending_at_store().empty());
  for (auto const pending : storage.pending_at_store()) {
    CHECK(pending == 0);
  }
}

// A freezes and persists the packet, B imports and persists it, and only then
// does each side transmit. Proves the whole initial-state exchange plus the
// structural guarantees of the imported graph.
void TestInitialSyncAcrossReplicas() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildSharedNode(a, 4001, 4002, 4003, 7);
  b.sync->ExpectInitialNode(fixture.node_id);

  // Sender: persist the frozen packet, then send it.
  a.watched.ResetWatch();
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  AllStoresBeforeAnySend(a.watched);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);

  auto const a_node = a.sync->FindNode(fixture.node_id);
  auto const a_state = SyncStateOf(a_node, fixture.share_to_b);
  CHECK(a_state->GetInitialSyncPhase() == InitialSyncPhase::Pending);
  CHECK(a_state->pending_initial_packet_id.is_valid());
  auto const frozen_packet = a_state->pending_initial_packet;
  CHECK(network.PeekNext(kEndpointA, kEndpointB) == frozen_packet);

  NodeStateFrame sent_frame;
  CHECK(DecodeNodeStateFrame(frozen_packet, sent_frame));
  CHECK(sent_frame.target_node_id == fixture.node_id);
  CHECK(sent_frame.destination_share_id == fixture.share_to_b);
  CHECK(sent_frame.packet_id == a_state->pending_initial_packet_id);

  // Receiver: import and persist, then acknowledge.
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  AllStoresBeforeAnySend(b.watched);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);

  auto const b_node = b.sync->FindNode(fixture.node_id);
  CHECK(b_node.is_valid());
  CHECK(ValueOf(b_node) == 7);

  // Independent C++ instances with matching logical identities.
  CHECK(static_cast<void const*>(a_node.operator->()) !=
        static_cast<void const*>(b_node.operator->()));
  CHECK(a_node.id() == b_node.id());
  CHECK(a_node->shares.size() == 2);
  CHECK(b_node->shares.size() == 2);
  for (std::size_t i = 0; i < b_node->shares.size(); ++i) {
    // Share relationship identity is shared topology: it must not be
    // re-derived from a receiver-side AddShare Event.
    CHECK(b_node->shares[i].share_id == a_node->shares[i].share_id);
    CHECK(b_node->shares[i].link.id() == a_node->shares[i].link.id());
    b_node->shares[i].link.Load();
    a_node->shares[i].link.Load();
    CHECK(static_cast<void const*>(b_node->shares[i].link.operator->()) !=
          static_cast<void const*>(a_node->shares[i].link.operator->()));
    CHECK(b_node->shares[i].link->EndpointUid() ==
          a_node->shares[i].link->EndpointUid());
  }
  CHECK(b_node->shares[0].link->EndpointUid() == kEndpointA);
  CHECK(b_node->shares[1].link->EndpointUid() == kEndpointB);

  // Receiver-local sync state is its own, created by replaying the imported
  // shared journal, and keyed by the shared relationship identity.
  CHECK(b_node->link_sync_states.size() == 2);
  auto const b_state = SyncStateOf(b_node, fixture.share_to_b);
  CHECK(b_state->share_id == fixture.share_to_b);
  CHECK(b_state.id() != a_state.id());
  CHECK(b_state->received_initial_packet_id == sent_frame.packet_id);
  CHECK(b_state->GetInitialSyncPhase() == InitialSyncPhase::Complete);
  // The sender's delivery state did not travel: neither its objects nor its
  // phase reached the receiver's storage.
  for (auto const& entry : a_node->link_sync_states) {
    CHECK(entry.is_valid());
    CHECK(b.storage.Enumerate(entry.id()).empty());
    CHECK(entry.id() != b_state.id());
  }
  CHECK(b_state->pending_initial_packet.empty());
  CHECK(!b_state->pending_initial_packet_id.is_valid());
  CHECK(SyncStateOf(b_node, fixture.share_to_a)->GetInitialSyncPhase() ==
        InitialSyncPhase::Complete);

  // A is still Pending until the ACK arrives: a successful transport write is
  // not an acknowledgement.
  CHECK(a_state->GetInitialSyncPhase() == InitialSyncPhase::Pending);
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(a_state->GetInitialSyncPhase() == InitialSyncPhase::Complete);
  CHECK(a_state->pending_initial_packet.empty());
  CHECK(!a_state->pending_initial_packet_id.is_valid());

  // Nothing is sent for an already acknowledged relationship.
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 0);

}

// Lost ACK: the retry is the persisted packet byte for byte, the receiver
// recognizes the duplicate without re-applying it, and acknowledges again.
void TestLostAckExactRetryAndDuplicate() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildSharedNode(a, 4101, 4102, 4103, 11);
  b.sync->ExpectInitialNode(fixture.node_id);
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);

  auto const a_node = a.sync->FindNode(fixture.node_id);
  auto const a_state = SyncStateOf(a_node, fixture.share_to_b);
  auto const frozen_packet = a_state->pending_initial_packet;

  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  auto const b_node = b.sync->FindNode(fixture.node_id);
  auto const b_state = SyncStateOf(b_node, fixture.share_to_b);
  auto const b_journal_size = b_node->journal.size();
  auto const b_state_id = b_state.id();

  // Drop the ACK. A stays Pending with the exact packet.
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
  CHECK(network.DropNext(kEndpointB, kEndpointA));
  CHECK(a_state->GetInitialSyncPhase() == InitialSyncPhase::Pending);
  CHECK(a_state->pending_initial_packet == frozen_packet);

  // The Node moves on after the packet was frozen. The retry must not carry
  // the newer state: incremental replication is a later milestone.
  SharedValueNode::ptr a_concrete = a_node;
  a_concrete.Load();
  SetValue(*a_concrete, 99);
  a_concrete.Save();
  CHECK(a_state->pending_initial_packet == frozen_packet);

  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);
  CHECK(network.PeekNext(kEndpointA, kEndpointB) == frozen_packet);

  // Duplicate: no re-import, no second Node, no extra journal or Shares, no
  // reset of receiver progress, no write at all — just another ACK.
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.watched.pending_at_store().empty());
  CHECK(b.sync->FindNode(fixture.node_id).operator->() ==
        b_node.operator->());
  CHECK(ValueOf(b_node) == 11);
  CHECK(b_node->journal.size() == b_journal_size);
  CHECK(b_node->shares.size() == 2);
  CHECK(b_node->link_sync_states.size() == 2);
  CHECK(SyncStateOf(b_node, fixture.share_to_b).id() == b_state_id);
  CHECK(b_state->received_initial_packet_id == a_state->pending_initial_packet_id);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);

  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(a_state->GetInitialSyncPhase() == InitialSyncPhase::Complete);
  CHECK(ValueOf(b_node) == 11);

}

// Sender restart while Pending: the same packet id and the same bytes come
// back from storage and are retried; they are never regenerated.
void TestSenderRestartWhilePending() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildSharedNode(a, 4201, 4202, 4203, 21);
  b.sync->ExpectInitialNode(fixture.node_id);
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);

  ae::ObjId packet_id;
  std::vector<std::uint8_t> frozen_packet;
  {
    auto const state =
        SyncStateOf(a.sync->FindNode(fixture.node_id), fixture.share_to_b);
    packet_id = state->pending_initial_packet_id;
    frozen_packet = state->pending_initial_packet;
  }
  // The in-flight packet is lost while the sender is down.
  CHECK(network.DropNext(kEndpointA, kEndpointB));

  a.Stop();
  a.Start();
  ReloadAndRegister(a, fixture.node_id);

  auto const a_node = a.sync->FindNode(fixture.node_id);
  CHECK(a_node.is_valid());
  CHECK(a_node->shares.size() == 2);
  CHECK(a_node->shares[1].share_id == fixture.share_to_b);
  auto const a_state = SyncStateOf(a_node, fixture.share_to_b);
  CHECK(a_state->GetInitialSyncPhase() == InitialSyncPhase::Pending);
  CHECK(a_state->pending_initial_packet_id == packet_id);
  CHECK(a_state->pending_initial_packet == frozen_packet);

  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  CHECK(network.PeekNext(kEndpointA, kEndpointB) == frozen_packet);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(a_state->GetInitialSyncPhase() == InitialSyncPhase::Complete);
  CHECK(ValueOf(b.sync->FindNode(fixture.node_id)) == 21);

}

// Receiver restart after it applied the snapshot but before the sender saw an
// ACK: the duplicate is still recognized from persisted state.
void TestReceiverRestartAfterApply() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildSharedNode(a, 4301, 4302, 4303, 31);
  b.sync->ExpectInitialNode(fixture.node_id);
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DropNext(kEndpointB, kEndpointA));

  ae::ObjId b_state_id;
  std::size_t b_journal_size = 0;
  {
    auto const b_node = b.sync->FindNode(fixture.node_id);
    b_state_id = SyncStateOf(b_node, fixture.share_to_b).id();
    b_journal_size = b_node->journal.size();
  }

  b.Stop();
  b.Start();
  ReloadAndRegister(b, fixture.node_id);

  auto const b_node = b.sync->FindNode(fixture.node_id);
  CHECK(b_node.is_valid());
  auto const b_state = SyncStateOf(b_node, fixture.share_to_b);
  CHECK(b_state.id() == b_state_id);
  CHECK(b_state->share_id == fixture.share_to_b);
  CHECK(b_state->GetInitialSyncPhase() == InitialSyncPhase::Complete);

  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.watched.pending_at_store().empty());
  CHECK(ValueOf(b_node) == 31);
  CHECK(b_node->journal.size() == b_journal_size);
  CHECK(b_node->link_sync_states.size() == 2);
  CHECK(SyncStateOf(b_node, fixture.share_to_b).id() == b_state_id);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);

  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(SyncStateOf(a.sync->FindNode(fixture.node_id), fixture.share_to_b)
            ->GetInitialSyncPhase() == InitialSyncPhase::Complete);

}

// Sender restart after the ACK was persisted: the relationship stays Complete
// and no initial packet is sent again.
void TestSenderRestartAfterComplete() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildSharedNode(a, 4401, 4402, 4403, 41);
  b.sync->ExpectInitialNode(fixture.node_id);
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  a.Stop();
  a.Start();
  ReloadAndRegister(a, fixture.node_id);

  auto const a_state =
      SyncStateOf(a.sync->FindNode(fixture.node_id), fixture.share_to_b);
  CHECK(a_state->GetInitialSyncPhase() == InitialSyncPhase::Complete);
  CHECK(a_state->pending_initial_packet.empty());
  CHECK(!a_state->pending_initial_packet_id.is_valid());

  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 0);

}

// One Link carries several SharedNodes: a packet is applied to the SharedNode
// named by target_node_id and to no other.
void TestRoutingByTargetNodeId() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const first = BuildSharedNode(a, 4501, 4502, 4503, 51);
  auto const second = BuildSharedNode(a, 4511, 4512, 4513, 52);
  b.sync->ExpectInitialNode(first.node_id);
  b.sync->ExpectInitialNode(second.node_id);

  a.sync->SyncInitialState(first.node_id, first.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  CHECK(b.sync->FindNode(first.node_id).is_valid());
  CHECK(!b.sync->FindNode(second.node_id).is_valid());
  CHECK(b.storage.Enumerate(second.node_id).empty());
  CHECK(ValueOf(b.sync->FindNode(first.node_id)) == 51);

  a.sync->SyncInitialState(second.node_id, second.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  CHECK(ValueOf(b.sync->FindNode(first.node_id)) == 51);
  CHECK(ValueOf(b.sync->FindNode(second.node_id)) == 52);
  CHECK(first.share_to_b != second.share_to_b);

}

// Untrusted bytes may not create a root this replica is not waiting for.
void TestUnexpectedNodeIsRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildSharedNode(a, 4601, 4602, 4603, 61);
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);

  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(!b.sync->FindNode(fixture.node_id).is_valid());
  CHECK(b.storage.Enumerate(fixture.node_id).empty());
  CHECK(b.watched.pending_at_store().empty());
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);

}

// Deterministic transport controls: directional outage, duplicate delivery,
// and reconnect, all without threads or sleeps.
void TestTransportDeterministicControls() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildSharedNode(a, 4701, 4702, 4703, 71);
  b.sync->ExpectInitialNode(fixture.node_id);

  network.Disconnect(kEndpointA, kEndpointB);
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 0);
  CHECK(!b.sync->FindNode(fixture.node_id).is_valid());

  network.Reconnect(kEndpointA, kEndpointB);
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);

  CHECK(network.DuplicateNext(kEndpointA, kEndpointB));
  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 2);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));

  auto const b_node = b.sync->FindNode(fixture.node_id);
  CHECK(b_node.is_valid());
  CHECK(ValueOf(b_node) == 71);
  CHECK(b_node->shares.size() == 2);
  CHECK(b_node->link_sync_states.size() == 2);
  // Both copies were acknowledged, the state was applied once.
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 2);

}

// An ACK is only an acknowledgement when it comes from the endpoint the
// relationship points at. Packet, node, and share ids prove nothing about the
// sender.
void TestAckMustComeFromRelationshipEndpoint() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  a.Start();
  ObserverEndpoint b{network, kEndpointB};
  ObserverEndpoint c{network, kEndpointC};

  // C is a participant of the shared topology, just not the destination of
  // the relationship being synchronized.
  auto const fixture = BuildSharedNode(a, 4801, 4802, 4803, 81, 4804);
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  CHECK(network.DropNext(kEndpointA, kEndpointB));

  auto const a_node = a.sync->FindNode(fixture.node_id);
  auto const a_state = SyncStateOf(a_node, fixture.share_to_b);
  CHECK(a_state->GetInitialSyncPhase() == InitialSyncPhase::Pending);
  auto const frozen_packet = a_state->pending_initial_packet;

  auto const ack_bytes = EncodeAckFrame(AckFrame{
      .packet_id = a_state->pending_initial_packet_id,
      .target_node_id = fixture.node_id,
      .destination_share_id = fixture.share_to_b,
  });

  c.transport.Send(kEndpointA, ack_bytes);
  a.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointC, kEndpointA));
  CHECK(a_state->GetInitialSyncPhase() == InitialSyncPhase::Pending);
  CHECK(a_state->pending_initial_packet == frozen_packet);
  CHECK(a.watched.pending_at_store().empty());

  b.transport.Send(kEndpointA, ack_bytes);
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(a_state->GetInitialSyncPhase() == InitialSyncPhase::Complete);
  CHECK(a_state->pending_initial_packet.empty());
}

// A NodeState from an endpoint the snapshot topology does not contain is
// rejected before a single byte reaches real storage, both as a first
// delivery and as a replay of an already applied packet.
void TestWrongSourceNodeStateRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();
  ObserverEndpoint c{network, kEndpointC};

  auto const fixture = BuildSharedNode(a, 4901, 4902, 4903, 91);
  b.sync->ExpectInitialNode(fixture.node_id);
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  auto const packet = network.PeekNext(kEndpointA, kEndpointB);
  CHECK(network.DropNext(kEndpointA, kEndpointB));

  // C is not a Link in the snapshot topology.
  c.transport.Send(kEndpointB, packet);
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointC, kEndpointB));
  CHECK(!b.sync->FindNode(fixture.node_id).is_valid());
  CHECK(b.storage.Enumerate(fixture.node_id).empty());
  CHECK(b.storage.Enumerate(ae::ObjId{4902}).empty());
  CHECK(b.storage.Enumerate(ae::ObjId{4903}).empty());
  CHECK(b.watched.pending_at_store().empty());
  CHECK(network.PendingCount(kEndpointB, kEndpointC) == 0);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
  CHECK(c.received == 0);

  // The same packet from the real sender is accepted.
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.sync->FindNode(fixture.node_id).is_valid());
  CHECK(ValueOf(b.sync->FindNode(fixture.node_id)) == 91);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 1);
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));

  // Replaying the applied packet from the wrong endpoint is not acknowledged.
  c.transport.Send(kEndpointB, packet);
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointC, kEndpointB));
  CHECK(b.watched.pending_at_store().empty());
  CHECK(network.PendingCount(kEndpointB, kEndpointC) == 0);
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
  CHECK(c.received == 0);
  CHECK(ValueOf(b.sync->FindNode(fixture.node_id)) == 91);
}

// A NodeState whose relationship ends at some other endpoint is rejected the
// same way, even though the sender is a known participant.
void TestWrongDestinationNodeStateRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildSharedNode(a, 5001, 5002, 5003, 101);
  b.sync->ExpectInitialNode(fixture.node_id);

  auto const a_node = a.sync->FindNode(fixture.node_id);
  auto const frame = NodeStateFrame{
      .packet_id = ae::ObjId{5099},
      .target_node_id = fixture.node_id,
      // A's own relationship, whose Link endpoint is A, not B.
      .destination_share_id = fixture.share_to_a,
      .payload = SerializeNetworkSharedObjectGraph(*a_node),
  };
  a.transport->Send(kEndpointB, EncodeNodeStateFrame(frame));

  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(!b.sync->FindNode(fixture.node_id).is_valid());
  CHECK(b.storage.Enumerate(fixture.node_id).empty());
  CHECK(b.storage.Enumerate(ae::ObjId{5002}).empty());
  CHECK(b.watched.pending_at_store().empty());
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
}

// An expected target is not enough: a payload that does not parse, or whose
// root is not a SharedNode, is rejected with real storage untouched.
void TestMalformedNodeStateRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildSharedNode(a, 5201, 5202, 5203, 121);
  b.sync->ExpectInitialNode(fixture.node_id);
  b.sync->ExpectInitialNode(ae::ObjId{5203});

  auto const a_node = a.sync->FindNode(fixture.node_id);
  auto truncated_payload = SerializeNetworkSharedObjectGraph(*a_node);
  truncated_payload.pop_back();
  a.transport->Send(kEndpointB,
                    EncodeNodeStateFrame(NodeStateFrame{
                        .packet_id = ae::ObjId{5299},
                        .target_node_id = fixture.node_id,
                        .destination_share_id = fixture.share_to_b,
                        .payload = truncated_payload,
                    }));
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(!b.sync->FindNode(fixture.node_id).is_valid());
  CHECK(b.storage.Enumerate(fixture.node_id).empty());
  CHECK(b.watched.pending_at_store().empty());
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);

  // Parsable, but the root is a Link, not a SharedNode.
  auto link_b = MemoryLink::ptr::Declare(
      ae::CreateWith{*a.domain}.with_id(ae::ObjId{5203}));
  link_b.Load();
  CHECK(link_b.is_loaded());
  a.transport->Send(kEndpointB,
                    EncodeNodeStateFrame(NodeStateFrame{
                        .packet_id = ae::ObjId{5298},
                        .target_node_id = ae::ObjId{5203},
                        .destination_share_id = fixture.share_to_b,
                        .payload = SerializeNetworkSharedObjectGraph(*link_b),
                    }));
  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(!b.sync->FindNode(ae::ObjId{5203}).is_valid());
  CHECK(b.storage.Enumerate(ae::ObjId{5203}).empty());
  CHECK(b.watched.pending_at_store().empty());
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
}

// Storage that counts writes and refuses none: used to prove a malformed
// payload writes nothing at all.
class CountingStorage final : public ae::IDomainStorage {
 public:
  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override {
    ++store_count;
    return inner.Store(query);
  }

  ae::ClassList Enumerate(ae::ObjId const& obj_id) override {
    return inner.Enumerate(obj_id);
  }

  ae::DomainLoad Load(ae::DomainQuery const& query) override {
    return inner.Load(query);
  }

  void Remove(ae::ObjId const& obj_id) override { inner.Remove(obj_id); }
  void CleanUp() override { inner.CleanUp(); }

  ae::RamDomainStorage inner;
  std::size_t store_count = 0;
};

// Malformed payload leaves the target storage untouched: the whole graph is
// parsed into an intermediate before the first write.
void TestMalformedPayloadWritesNothing() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  a.Start();

  auto const fixture = BuildSharedNode(a, 5101, 5102, 5103, 111);
  auto const payload =
      SerializeNetworkSharedObjectGraph(*a.sync->FindNode(fixture.node_id));
  CHECK(payload.size() > 8);

  {
    CountingStorage target;
    auto truncated = payload;
    truncated.pop_back();
    CHECK(!ImportObjectGraphPayload(truncated, target));
    CHECK(target.store_count == 0);
  }
  {
    CountingStorage target;
    std::vector<std::uint8_t> const head(payload.begin(),
                                         payload.begin() + 9);
    CHECK(!ImportObjectGraphPayload(head, target));
    CHECK(target.store_count == 0);
  }
  {
    CountingStorage target;
    auto trailing = payload;
    trailing.push_back(0);
    CHECK(!ImportObjectGraphPayload(trailing, target));
    CHECK(target.store_count == 0);
  }
  {
    CountingStorage target;
    CHECK(ImportObjectGraphPayload(payload, target));
    CHECK(target.store_count > 0);
    CHECK(!target.Enumerate(fixture.node_id).empty());
  }
}

void TestMalformedClassLayersInNodeStateRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildSharedNode(a, 5201, 5202, 5203, 222);

  // Craft a payload where an object has two unrelated registered class layers
  ae::RamDomainStorage bad_storage;
  bad_storage.state[fixture.node_id] = ae::RamDomainStorage::ClassData{
      {SharedValueNode::kClassId, {{1, std::vector<std::uint8_t>{1, 2, 3}}}},
      {apptraverse::example::shared_node::Client::kClassId,
       {{1, std::vector<std::uint8_t>{4, 5, 6}}}},
  };

  auto const payload = SerializeObjectGraph(bad_storage);

  // Before delivery explicitly prove parser succeeds but class-chain validation fails
  ae::RamDomainStorage parsed_check;
  CHECK(DeserializeObjectGraph(payload, parsed_check) == true);
  CHECK(ValidateStoredClassChains(parsed_check) == false);

  b.sync->ExpectInitialNode(fixture.node_id);
  a.transport->Send(
      kEndpointB,
      EncodeNodeStateFrame(NodeStateFrame{.packet_id = ae::ObjId{991},
                                          .target_node_id = fixture.node_id,
                                          .destination_share_id = fixture.share_to_b,
                                          .payload = payload}));

  b.watched.ResetWatch();
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  // Payload must be rejected before LoadRoot / writing to b's storage, no ACK sent
  CHECK(b.watched.pending_at_store().empty());
  CHECK(!b.sync->FindNode(fixture.node_id).is_valid());
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
}

// Frames are canonical: no trailing bytes, no zero ids.
void TestFrameDecodingIsStrict() {
  NodeStateFrame const node_state{
      .packet_id = ae::ObjId{11},
      .target_node_id = ae::ObjId{12},
      .destination_share_id = ae::ObjId{13},
      .payload = {1, 2, 3},
  };
  auto const node_state_bytes = EncodeNodeStateFrame(node_state);

  NodeStateFrame decoded_node_state;
  CHECK(DecodeNodeStateFrame(node_state_bytes, decoded_node_state));
  CHECK(decoded_node_state.packet_id == node_state.packet_id);
  CHECK(decoded_node_state.target_node_id == node_state.target_node_id);
  CHECK(decoded_node_state.destination_share_id ==
        node_state.destination_share_id);
  CHECK(decoded_node_state.payload == node_state.payload);

  auto node_state_trailing = node_state_bytes;
  node_state_trailing.push_back(0);
  CHECK(!DecodeNodeStateFrame(node_state_trailing, decoded_node_state));

  auto node_state_truncated = node_state_bytes;
  node_state_truncated.pop_back();
  CHECK(!DecodeNodeStateFrame(node_state_truncated, decoded_node_state));

  CHECK(!DecodeNodeStateFrame(
      EncodeNodeStateFrame(NodeStateFrame{.packet_id = ae::ObjId{},
                                          .target_node_id = ae::ObjId{12},
                                          .destination_share_id = ae::ObjId{13},
                                          .payload = {}}),
      decoded_node_state));
  CHECK(!DecodeNodeStateFrame(
      EncodeNodeStateFrame(NodeStateFrame{.packet_id = ae::ObjId{11},
                                          .target_node_id = ae::ObjId{},
                                          .destination_share_id = ae::ObjId{13},
                                          .payload = {}}),
      decoded_node_state));
  CHECK(!DecodeNodeStateFrame(
      EncodeNodeStateFrame(NodeStateFrame{.packet_id = ae::ObjId{11},
                                          .target_node_id = ae::ObjId{12},
                                          .destination_share_id = ae::ObjId{},
                                          .payload = {}}),
      decoded_node_state));

  AckFrame const ack{
      .packet_id = ae::ObjId{21},
      .target_node_id = ae::ObjId{22},
      .destination_share_id = ae::ObjId{23},
  };
  auto const ack_bytes = EncodeAckFrame(ack);

  AckFrame decoded_ack;
  CHECK(DecodeAckFrame(ack_bytes, decoded_ack));
  CHECK(decoded_ack.packet_id == ack.packet_id);
  CHECK(decoded_ack.target_node_id == ack.target_node_id);
  CHECK(decoded_ack.destination_share_id == ack.destination_share_id);

  auto ack_trailing = ack_bytes;
  ack_trailing.push_back(0);
  CHECK(!DecodeAckFrame(ack_trailing, decoded_ack));

  auto ack_truncated = ack_bytes;
  ack_truncated.pop_back();
  CHECK(!DecodeAckFrame(ack_truncated, decoded_ack));

  CHECK(!DecodeAckFrame(
      EncodeAckFrame(AckFrame{.packet_id = ae::ObjId{},
                              .target_node_id = ae::ObjId{22},
                              .destination_share_id = ae::ObjId{23}}),
      decoded_ack));

  // A NodeState must not decode as an Ack, and neither decodes from junk.
  CHECK(!DecodeAckFrame(node_state_bytes, decoded_ack));
  CHECK(!DecodeNodeStateFrame(ack_bytes, decoded_node_state));

  SyncFrameType type{};
  CHECK(PeekSyncFrameType(node_state_bytes, type));
  CHECK(type == SyncFrameType::kNodeState);
  CHECK(PeekSyncFrameType(ack_bytes, type));
  CHECK(type == SyncFrameType::kAck);
  CHECK(!PeekSyncFrameType(std::vector<std::uint8_t>{}, type));
  CHECK(!PeekSyncFrameType(std::vector<std::uint8_t>{1}, type));
  CHECK(!PeekSyncFrameType(std::vector<std::uint8_t>{99, 1}, type));
  CHECK(!PeekSyncFrameType(std::vector<std::uint8_t>{1, 99}, type));
}

void TestReceiverLocalSentinelCollisionRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  auto const fixture = BuildSharedNode(a, 5301, 5302, 5303, 777);

  // Create a receiver-local sentinel object on B whose ObjId collides with
  // one of the objects inside A's node graph (link_a = 5302)
  auto sentinel = MakeMemoryLink(*b.domain, ae::ObjId{5302}, "sentinel-endpoint-b");
  sentinel.Save();

  b.sync->ExpectInitialNode(fixture.node_id);
  a.sync->SyncInitialState(fixture.node_id, fixture.share_to_b);

  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));

  // B must reject snapshot: no new target SharedNode registered
  CHECK(!b.sync->FindNode(fixture.node_id).is_valid());

  // Sentinel must remain untouched in class and value
  auto loaded_sentinel = b.domain->Find(ae::ObjId{5302});
  CHECK(loaded_sentinel);
  CHECK(loaded_sentinel->GetClassId() == MemoryLink::kClassId);
  auto& mem_link = static_cast<MemoryLink&>(*loaded_sentinel);
  CHECK(mem_link.endpoint_uid == "sentinel-endpoint-b");

  // No ACK sent back to A
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
}

void TestSnapshotWithTwoSourceSharesRejected() {
  MemoryNetwork network;
  Replica a{network, kEndpointA, kEndpointB};
  Replica b{network, kEndpointB, kEndpointA};
  a.Start();
  b.Start();

  // Create a SharedNode on A with two shares pointing to kEndpointA, and one share pointing to kEndpointB
  ae::ObjId const node_id{5401};
  ae::ObjId const link_a1_id{5402};
  ae::ObjId const link_a2_id{5403};
  ae::ObjId const link_b_id{5404};

  auto node =
      SharedValueNode::ptr::Create(ae::CreateWith{*a.domain}.with_id(node_id));
  InitializeRuntimeNode(*node);
  auto link_a1 = MakeMemoryLink(*a.domain, link_a1_id, kEndpointA);
  auto link_a2 = MakeMemoryLink(*a.domain, link_a2_id, kEndpointA);
  auto link_b = MakeMemoryLink(*a.domain, link_b_id, kEndpointB);
  node->InstallLocalShare(link_a1, ShareAccess::ReadWrite);
  node->InstallLocalShare(link_a2, ShareAccess::ReadWrite);
  node->InstallLocalShare(link_b, ShareAccess::ReadWrite);
  SetValue(*node, 42);
  node.Save();
  link_a1.Save();
  link_a2.Save();
  link_b.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }

  a.sync->RegisterNode(node);
  b.sync->ExpectInitialNode(node_id);

  auto const share_to_b = node->shares[2].share_id;
  a.sync->SyncInitialState(node_id, share_to_b);

  CHECK(network.PendingCount(kEndpointA, kEndpointB) == 1);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));

  // B must reject snapshot:
  // - target Node absent from production Domain;
  // - target Node absent from production storage;
  // - no imported Link objects;
  // - no ACK.
  CHECK(!b.sync->FindNode(node_id).is_valid());
  CHECK(b.domain->Find(node_id) == nullptr);
  CHECK(b.storage.Enumerate(node_id).empty());
  CHECK(b.storage.Enumerate(link_a1_id).empty());
  CHECK(b.storage.Enumerate(link_a2_id).empty());
  CHECK(b.storage.Enumerate(link_b_id).empty());
  CHECK(network.PendingCount(kEndpointB, kEndpointA) == 0);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::shared_node::EnsureSharedNodeDemoRegistration();

  apptraverse::test::TestInitialSyncAcrossReplicas();
  apptraverse::test::TestLostAckExactRetryAndDuplicate();
  apptraverse::test::TestSenderRestartWhilePending();
  apptraverse::test::TestReceiverRestartAfterApply();
  apptraverse::test::TestSenderRestartAfterComplete();
  apptraverse::test::TestRoutingByTargetNodeId();
  apptraverse::test::TestUnexpectedNodeIsRejected();
  apptraverse::test::TestTransportDeterministicControls();
  apptraverse::test::TestAckMustComeFromRelationshipEndpoint();
  apptraverse::test::TestWrongSourceNodeStateRejected();
  apptraverse::test::TestWrongDestinationNodeStateRejected();
  apptraverse::test::TestMalformedNodeStateRejected();
  apptraverse::test::TestMalformedClassLayersInNodeStateRejected();
  apptraverse::test::TestMalformedPayloadWritesNothing();
  apptraverse::test::TestFrameDecodingIsStrict();
  apptraverse::test::TestReceiverLocalSentinelCollisionRejected();
  apptraverse::test::TestSnapshotWithTwoSourceSharesRejected();

  std::cout << "shared_node_initial_sync_test OK\n";
  return 0;
}
