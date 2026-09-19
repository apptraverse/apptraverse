#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/link.h"
#include "apptraverse/memory_transport.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/share_offer.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/shared_sync_runtime.h"
#include "apptraverse/sync_frame.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

std::string const kEndpointA = "endpoint-a";
std::string const kEndpointB = "endpoint-b";
std::string const kEndpointC = "endpoint-c";
std::string const kLocalSecret = "JOIN_LOCAL_SECRET_do_not_ship";
std::string const kPrivateMarker = "PRIVATE_NODE_Z_not_shared";

class JoinLocalSecret : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::JoinLocalSecret",
                           JoinLocalSecret, ae::Obj, 0)

 protected:
  JoinLocalSecret() = default;

 public:
  explicit JoinLocalSecret(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(mark))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, mark);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, mark);
  }

  std::string mark;
};

class AddJoinRecordEvent;
class JoinRecordNode
    : public apptraverse::NodeFor<JoinRecordNode, apptraverse::SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::JoinRecordNode", JoinRecordNode,
                           SharedNode, 0)

 protected:
  JoinRecordNode() = default;

 public:
  explicit JoinRecordNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(local_secret))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, records, local_secret);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, records, local_secret);
  }

  std::vector<std::string> records;
  apptraverse::LocalPtr<JoinLocalSecret> local_secret;

  void Apply(AddJoinRecordEvent const& event);
};

class AddJoinRecordEvent
    : public apptraverse::EventFor<JoinRecordNode, AddJoinRecordEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::AddJoinRecordEvent",
                           AddJoinRecordEvent, Event, 0)

 protected:
  AddJoinRecordEvent() = default;

 public:
  explicit AddJoinRecordEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, text);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text);
  }

  std::string text;
};

class JoinOtherNode
    : public apptraverse::NodeFor<JoinOtherNode, apptraverse::SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::JoinOtherNode", JoinOtherNode,
                           SharedNode, 0)

 protected:
  JoinOtherNode() = default;

 public:
  explicit JoinOtherNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(tag))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, tag);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, tag);
  }

  std::int32_t tag{0};
};

void JoinRecordNode::Apply(AddJoinRecordEvent const& event) {
  records.push_back(event.text);
  NoteMaterializedChange();
}

APPTRAVERSE_REGISTER(JoinLocalSecret);
APPTRAVERSE_REGISTER(JoinRecordNode);
APPTRAVERSE_REGISTER(AddJoinRecordEvent);
APPTRAVERSE_REGISTER(JoinOtherNode);

class JoinBinding : public apptraverse::NodeFor<JoinBinding> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::JoinBinding", JoinBinding, Node,
                           0)

 protected:
  JoinBinding() = default;

 public:
  explicit JoinBinding(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, node_ids);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, node_ids);
  }

  std::vector<std::uint32_t> node_ids;
};

APPTRAVERSE_REGISTER(JoinBinding);

ae::ObjId const kJoinBindingId{0x4A4F494E};

MemoryLink::ptr MakeMemoryLink(ae::Domain& domain, std::string endpoint);

bool AcceptAll(SharedSyncRuntime::ShareOfferView const&) { return true; }

bool Contains(std::vector<std::uint8_t> const& bytes, std::string const& needle) {
  if (needle.empty() || bytes.size() < needle.size()) {
    return false;
  }
  std::string_view const view(reinterpret_cast<char const*>(bytes.data()),
                              bytes.size());
  return view.find(needle) != std::string_view::npos;
}

struct Replica {
  Replica(MemoryNetwork& network, std::string endpoint_uid)
      : storage{},
        network_{network},
        endpoint_uid_{std::move(endpoint_uid)} {}

  void Start() {
    binds = 0;
    domain = std::make_unique<ae::Domain>(storage);
    transport =
        std::make_unique<MemoryTransport>(network_, endpoint_uid_);
    sync = std::make_unique<SharedSyncRuntime>(*domain, storage, *transport);
    sync->AllowStandaloneEventClass(AddJoinRecordEvent::kClassId);
    sync->SetShareOfferPolicy(AcceptAll);
    sync->SetLinkForEndpoint([this](std::string const& endpoint) {
      return MakeMemoryLink(*domain, endpoint);
    });
    sync->SetInitialNodeImportedCallback(
        [this](std::string const&, SharedNode::ptr node) {
          ++binds;
          JoinBinding::ptr binding;
          if (storage.Enumerate(kJoinBindingId).empty()) {
            binding = JoinBinding::ptr::Create(
                ae::CreateWith{*domain}.with_id(kJoinBindingId));
            InitializeRuntimeNode(*binding);
          } else {
            binding = JoinBinding::ptr::Declare(
                ae::CreateWith{*domain}.with_id(kJoinBindingId));
            binding.Load();
            CHECK(binding.is_loaded());
          }
          auto const id = node.id().id();
          bool found = false;
          for (auto existing : binding->node_ids) {
            if (existing == id) {
              found = true;
            }
          }
          if (!found) {
            binding->node_ids.push_back(id);
          }
          binding.Save();
          return true;
        });
  }

  void Stop() {
    sync.reset();
    transport.reset();
    domain.reset();
  }

  std::string const& endpoint() const { return endpoint_uid_; }

  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  std::unique_ptr<MemoryTransport> transport;
  std::unique_ptr<SharedSyncRuntime> sync;
  int binds{0};

 private:
  MemoryNetwork& network_;
  std::string endpoint_uid_;
};

MemoryLink::ptr MakeMemoryLink(ae::Domain& domain, std::string endpoint) {
  auto link =
      MemoryLink::ptr::Create(ae::CreateWith{domain}.with_id(
          ae::ObjId::GenerateUnique()));
  link->endpoint_uid = std::move(endpoint);
  link->heartbeat_interval_ms = 1000;
  InitializeRuntimeNode(*link);
  link.Save();
  return link;
}

void SaveSync(SharedNode::ptr node) {
  node.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }
}

JoinRecordNode::ptr MakeRecordNode(Replica& replica, std::string secret) {
  auto node = JoinRecordNode::ptr::Create(ae::CreateWith{*replica.domain});
  InitializeRuntimeNode(*node);
  auto hidden =
      JoinLocalSecret::ptr::Create(ae::CreateWith{*replica.domain});
  hidden->mark = std::move(secret);
  hidden.Save();
  node->local_secret = hidden;
  auto self = MakeMemoryLink(*replica.domain, replica.endpoint());
  node->AddShare(self, ShareAccess::ReadWrite);
  SaveSync(node);
  replica.sync->RegisterNode(node);
  return node;
}

void AddRecord(JoinRecordNode& node, std::string text, std::string origin,
               std::uint64_t sequence, std::uint64_t timestamp_us) {
  auto event = AddJoinRecordEvent::ptr::Create(ae::CreateWith{*node.domain});
  event->text = std::move(text);
  node.CommitShared(std::move(event),
                    SharedEventId{.origin_uid = std::move(origin),
                                  .origin_sequence = sequence},
                    SharedEventOrder{.timestamp_us = timestamp_us});
  JoinRecordNode::ptr::MakeFromThis(&node).Save();
}

JoinRecordNode::ptr AsRecord(SharedNode::ptr node) {
  JoinRecordNode::ptr concrete = node;
  concrete.Load();
  CHECK(concrete.is_loaded());
  return concrete;
}

struct Observed {
  SharedEventId id;
  std::uint64_t timestamp_us{0};
  std::string text;
};

std::vector<Observed> Observe(JoinRecordNode const& node) {
  std::vector<Observed> out;
  for (auto const& record : node.journal) {
    if (!record.HasSharedIdentity()) {
      continue;
    }
    CHECK(record.event.is_valid());
    auto event = record.event;
    if (!event.is_loaded()) {
      event.Load();
    }
    CHECK(event->GetClassId() == AddJoinRecordEvent::kClassId);
    AddJoinRecordEvent::ptr concrete = event;
    CHECK(concrete.is_loaded());
    out.push_back(Observed{.id = record.identity,
                           .timestamp_us = record.order.timestamp_us,
                           .text = concrete->text});
  }
  std::sort(out.begin(), out.end(), [](Observed const& a, Observed const& b) {
    if (a.timestamp_us != b.timestamp_us) {
      return a.timestamp_us < b.timestamp_us;
    }
    if (a.id.origin_uid != b.id.origin_uid) {
      return a.id.origin_uid < b.id.origin_uid;
    }
    return a.id.origin_sequence < b.id.origin_sequence;
  });
  return out;
}

void ExpectSameSharedState(SharedNode::ptr left, SharedNode::ptr right) {
  CHECK(left.is_valid());
  CHECK(right.is_valid());
  CHECK(left.id() == right.id());
  CHECK(left->shares.size() == right->shares.size());
  for (std::size_t i = 0; i < left->shares.size(); ++i) {
    CHECK(left->shares[i].share_id == right->shares[i].share_id);
    CHECK(left->shares[i].share_id != left.id());
    CHECK(left->shares[i].GetAccess() == right->shares[i].GetAccess());
    CHECK(left->shares[i].link.is_valid());
    CHECK(right->shares[i].link.is_valid());
    if (!left->shares[i].link.is_loaded()) {
      left->shares[i].link.Load();
    }
    if (!right->shares[i].link.is_loaded()) {
      right->shares[i].link.Load();
    }
    CHECK(left->shares[i].link.id() == right->shares[i].link.id());
    CHECK(left->shares[i].link->EndpointUid() ==
          right->shares[i].link->EndpointUid());
  }
  auto const a = Observe(*AsRecord(left));
  auto const b = Observe(*AsRecord(right));
  CHECK(a.size() == b.size());
  auto const left_records = AsRecord(left)->records;
  auto const right_records = AsRecord(right)->records;
  CHECK(left_records == right_records);
  CHECK(left_records.size() == a.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    CHECK(a[i].id == b[i].id);
    CHECK(a[i].timestamp_us == b[i].timestamp_us);
    CHECK(a[i].text == b[i].text);
    CHECK(left_records[i] == a[i].text);
  }
  CHECK(!AsRecord(right)->local_secret.is_valid());
}

struct World {
  MemoryNetwork* network{nullptr};
  std::vector<Replica*> replicas;
  std::vector<std::string> forbidden;
};

std::string Diagnose(World const& world) {
  std::string out;
  for (auto* replica : world.replicas) {
    out += replica->endpoint();
    out += " offers";
    for (auto const& status : replica->sync->OfferStatuses()) {
      out += " [op=";
      out += std::to_string(status.operation_id.id());
      out += " node=";
      out += std::to_string(status.node_id.id());
      out += " phase=";
      out += std::to_string(static_cast<int>(status.phase));
      out += "]";
    }
    out += " queues";
    for (auto* other : world.replicas) {
      if (other == replica) {
        continue;
      }
      out += " ";
      out += other->endpoint();
      out += "=";
      out += std::to_string(
          world.network->PendingCount(replica->endpoint(), other->endpoint()));
    }
    out += "\n";
  }
  return out;
}

bool DeliverRound(World& world) {
  bool any = false;
  for (auto* from : world.replicas) {
    for (auto* to : world.replicas) {
      if (from == to) {
        continue;
      }
      auto const& bytes =
          world.network->PeekNext(from->endpoint(), to->endpoint());
      for (auto const& forbidden : world.forbidden) {
        if (Contains(bytes, forbidden)) {
          std::cerr << "forbidden bytes " << forbidden << " on "
                    << from->endpoint() << " -> " << to->endpoint() << '\n';
          CHECK(false);
        }
      }
      if (world.network->DeliverNext(from->endpoint(), to->endpoint())) {
        any = true;
      }
    }
  }
  return any;
}

void Drain(World& world) {
  for (int step = 0; step < 32; ++step) {
    if (!DeliverRound(world)) {
      return;
    }
  }
}

void PumpUntil(World& world, auto&& done, int max_steps) {
  for (int step = 0; step < max_steps; ++step) {
    if (done()) {
      return;
    }
    DeliverRound(world);
    for (auto* replica : world.replicas) {
      replica->sync->Service(0);
    }
    if (done()) {
      return;
    }
  }
  std::cerr << "pump stopped after " << max_steps << " steps\n"
            << Diagnose(world);
  CHECK(false);
}

void Restart(Replica& replica) {
  replica.Stop();
  replica.Start();
}

bool PhaseIs(Replica& replica, ae::ObjId operation, ShareOfferPhase phase) {
  return replica.sync->OfferPhase(operation) == phase;
}

std::uint64_t g_now = 0;

void ServiceRetry(Replica& replica, MemoryNetwork& network,
                  std::string const& to) {
  auto const before = network.PendingCount(replica.endpoint(), to);
  replica.sync->Service(g_now);
  if (network.PendingCount(replica.endpoint(), to) == before) {
    g_now += kShareOfferRetryIntervalUs;
    replica.sync->Service(g_now);
  }
}

void TestFrameRoundTrip() {
  ShareOfferFrame const offer{
      .packet_id = ae::ObjId{11},
      .operation_id = ae::ObjId{22},
      .target_node_id = ae::ObjId{33},
      .root_class_id = JoinRecordNode::kClassId,
      .access = static_cast<std::uint8_t>(ShareAccess::ReadOnly),
  };
  auto const bytes = EncodeShareOfferFrame(offer);
  CHECK(!Contains(bytes, kEndpointA));
  ShareOfferFrame decoded;
  CHECK(DecodeShareOfferFrame(bytes, decoded));
  CHECK(decoded.packet_id == offer.packet_id);
  CHECK(decoded.operation_id == offer.operation_id);
  CHECK(decoded.target_node_id == offer.target_node_id);
  CHECK(decoded.root_class_id == offer.root_class_id);
  CHECK(decoded.access == offer.access);
  auto damaged = bytes;
  damaged.back() ^= 0xFF;
  ShareOfferFrame ignored;
  CHECK(!DecodeShareOfferFrame(damaged, ignored));

  ShareDecisionFrame const decision{
      .packet_id = ae::ObjId{44},
      .operation_id = ae::ObjId{22},
      .target_node_id = ae::ObjId{33},
      .root_class_id = JoinRecordNode::kClassId,
      .access = static_cast<std::uint8_t>(ShareAccess::ReadWrite),
      .accepted = true,
  };
  auto const decision_bytes = EncodeShareDecisionFrame(decision);
  ShareDecisionFrame decision_out;
  CHECK(DecodeShareDecisionFrame(decision_bytes, decision_out));
  CHECK(decision_out.accepted);
  CHECK(decision_out.operation_id == decision.operation_id);
}

void TestParallelEndpointExpectations() {
  MemoryNetwork network;
  Replica a{network, kEndpointA};
  Replica b{network, kEndpointB};
  a.Start();
  b.Start();

  auto node_x = MakeRecordNode(a, kLocalSecret);
  auto node_y = MakeRecordNode(a, kLocalSecret);
  auto link_bx = MakeMemoryLink(*a.domain, kEndpointB);
  auto link_by = MakeMemoryLink(*a.domain, kEndpointB);
  node_x->AddShare(link_bx, ShareAccess::ReadWrite);
  node_y->AddShare(link_by, ShareAccess::ReadWrite);
  AddRecord(*node_x, "x-seed", kEndpointA, 1, 100);
  AddRecord(*node_y, "y-seed", kEndpointA, 1, 200);
  SaveSync(node_x);
  SaveSync(node_y);

  b.sync->ExpectInitialNodeFromEndpoint(kEndpointA, JoinRecordNode::kClassId,
                                        node_x.id());
  b.sync->ExpectInitialNodeFromEndpoint(kEndpointA, JoinRecordNode::kClassId,
                                        node_y.id());
  // Forgetting X must leave Y waiting.
  b.sync->ForgetInitialNodeExpectation(kEndpointA, node_x.id());
  b.sync->ExpectInitialNodeFromEndpoint(kEndpointA, JoinRecordNode::kClassId,
                                        node_x.id());
  b.sync->SetInitialNodeImportedCallback(
      [](std::string const&, SharedNode::ptr) { return true; });

  auto const share_x = node_x->shares[1].share_id;
  auto const share_y = node_y->shares[1].share_id;
  a.sync->SyncInitialState(node_x.id(), share_x);
  a.sync->SyncInitialState(node_y.id(), share_y);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.sync->FindNode(node_x.id()).is_valid());
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.sync->FindNode(node_y.id()).is_valid());
  CHECK(AsRecord(b.sync->FindNode(node_x.id()))->records.size() == 1);
  CHECK(AsRecord(b.sync->FindNode(node_y.id()))->records[0] == "y-seed");
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(network.DeliverNext(kEndpointB, kEndpointA));
}

void TestForgetOneExpectationLeavesTheOther() {
  MemoryNetwork network;
  Replica a{network, kEndpointA};
  Replica b{network, kEndpointB};
  a.Start();
  b.Start();
  auto node_x = MakeRecordNode(a, kLocalSecret);
  auto node_y = MakeRecordNode(a, kLocalSecret);
  node_x->AddShare(MakeMemoryLink(*a.domain, kEndpointB), ShareAccess::ReadWrite);
  node_y->AddShare(MakeMemoryLink(*a.domain, kEndpointB), ShareAccess::ReadWrite);
  AddRecord(*node_x, "only-x", kEndpointA, 1, 100);
  AddRecord(*node_y, "only-y", kEndpointA, 1, 200);
  SaveSync(node_x);
  SaveSync(node_y);
  b.sync->ExpectInitialNodeFromEndpoint(kEndpointA, JoinRecordNode::kClassId,
                                        node_x.id());
  b.sync->ExpectInitialNodeFromEndpoint(kEndpointA, JoinRecordNode::kClassId,
                                        node_y.id());
  b.sync->ForgetInitialNodeExpectation(kEndpointA, node_x.id());

  a.sync->SyncInitialState(node_x.id(), node_x->shares[1].share_id);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(!b.sync->FindNode(node_x.id()).is_valid());
  CHECK(b.storage.Enumerate(node_x.id()).empty());

  a.sync->SyncInitialState(node_y.id(), node_y->shares[1].share_id);
  CHECK(network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(b.sync->FindNode(node_y.id()).is_valid());
  CHECK(AsRecord(b.sync->FindNode(node_y.id()))->records[0] == "only-y");
  CHECK(!b.sync->FindNode(node_x.id()).is_valid());
}

struct Pair {
  MemoryNetwork network;
  Replica a;
  Replica b;
  Pair() : a(network, kEndpointA), b(network, kEndpointB) {
    a.Start();
    b.Start();
  }
  World world(std::vector<std::string> forbidden = {}) {
    return World{.network = &network,
                 .replicas = {&a, &b},
                 .forbidden = std::move(forbidden)};
  }
};

ae::ObjId Offer(Pair& pair, JoinRecordNode::ptr node, ShareAccess access) {
  auto remote = MakeMemoryLink(*pair.a.domain, pair.b.endpoint());
  return pair.a.sync->OfferNode(node, remote, access);
}

bool FullyJoined(Pair& pair, ae::ObjId operation, ae::ObjId node_id) {
  return PhaseIs(pair.a, operation, ShareOfferPhase::Complete) &&
         PhaseIs(pair.b, operation, ShareOfferPhase::Bound) &&
         pair.b.sync->FindNode(node_id).is_valid();
}

void TestOfferIsIdempotent() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 50);
  auto const first = Offer(pair, node, ShareAccess::ReadWrite);
  auto const second = Offer(pair, node, ShareAccess::ReadWrite);
  CHECK(first == second);
  CHECK(first != node.id());
  CHECK(pair.a.sync->LocalOfferIds().size() == 1);
  auto world = pair.world({kLocalSecret});
  PumpUntil(world, [&] { return FullyJoined(pair, first, node.id()); }, 40);
  CHECK(pair.a.sync->FindNode(node.id())->shares.size() == 2);
  CHECK(pair.a.sync->OfferStatuses().size() == 1);
}

void TestMainExchangeAndBothSides() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 100);
  auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
  auto const& offer_bytes = pair.network.PeekNext(kEndpointA, kEndpointB);
  CHECK(!offer_bytes.empty());
  CHECK(!Contains(offer_bytes, kEndpointA));
  CHECK(!Contains(offer_bytes, kLocalSecret));

  auto world = pair.world({kLocalSecret, kPrivateMarker});
  PumpUntil(world, [&] { return FullyJoined(pair, operation, node.id()); }, 40);

  auto private_node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*private_node, kPrivateMarker, kEndpointA, 1, 90);

  auto live_a = AsRecord(pair.a.sync->FindNode(node.id()));
  auto live_b = AsRecord(pair.b.sync->FindNode(node.id()));
  ExpectSameSharedState(live_a, live_b);
  CHECK(operation != node.id());
  CHECK(live_a->shares[1].share_id != operation);
  CHECK(live_a->shares[1].share_id != node.id());

  for (std::uint64_t i = 1; i <= 20; ++i) {
    AddRecord(*live_a, "A-" + std::to_string(i), kEndpointA, i + 1, 1000 + i);
    AddRecord(*live_b, "B-" + std::to_string(i), kEndpointB, i, 5000 + i);
  }
  PumpUntil(
      world,
      [&] {
        auto a = pair.a.sync->FindNode(node.id());
        auto b = pair.b.sync->FindNode(node.id());
        return a.is_valid() && b.is_valid() &&
               Observe(*AsRecord(a)).size() == 41 &&
               Observe(*AsRecord(b)).size() == 41;
      },
      400);
  ExpectSameSharedState(pair.a.sync->FindNode(node.id()),
                        pair.b.sync->FindNode(node.id()));
  Drain(world);
  CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) == 0);
  CHECK(pair.network.PendingCount(kEndpointB, kEndpointA) == 0);
  CHECK(!pair.b.domain->Find(private_node.id()));
  CHECK(pair.b.storage.Enumerate(private_node.id()).empty());
}

void TestSimultaneousRecords() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 10);
  auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
  auto world = pair.world({kLocalSecret});
  PumpUntil(world, [&] { return FullyJoined(pair, operation, node.id()); }, 40);
  auto live_a = AsRecord(pair.a.sync->FindNode(node.id()));
  auto live_b = AsRecord(pair.b.sync->FindNode(node.id()));
  for (std::uint64_t i = 1; i <= 20; ++i) {
    AddRecord(*live_a, "sim-A-" + std::to_string(i), kEndpointA, 100 + i,
              10000 + i);
    AddRecord(*live_b, "sim-B-" + std::to_string(i), kEndpointB, 100 + i,
              20000 + i);
  }
  PumpUntil(
      world,
      [&] {
        return Observe(*AsRecord(pair.a.sync->FindNode(node.id()))).size() ==
                   41 &&
               Observe(*AsRecord(pair.b.sync->FindNode(node.id()))).size() ==
                   41;
      },
      400);
  ExpectSameSharedState(pair.a.sync->FindNode(node.id()),
                        pair.b.sync->FindNode(node.id()));
}

void TestEventDuringInitialSyncAndRepeatSnapshot() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 100);
  auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) == 1);
  NodeStateFrame snapshot;
  CHECK(DecodeNodeStateFrame(pair.network.PeekNext(kEndpointA, kEndpointB),
                             snapshot));
  CHECK(!Contains(snapshot.payload, kLocalSecret));
  CHECK(pair.network.DuplicateNext(kEndpointA, kEndpointB));
  AddRecord(*AsRecord(pair.a.sync->FindNode(node.id())), "during-sync",
            kEndpointA, 2, 150);
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  auto imported = AsRecord(pair.b.sync->FindNode(node.id()));
  CHECK(imported->records.size() == 1);
  CHECK(imported->records[0] == "seed");
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(PhaseIs(pair.a, operation, ShareOfferPhase::Complete));
  pair.a.sync->Service(0);
  pair.a.sync->Service(0);
  CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) == 2);
  CHECK(pair.network.DeferNext(kEndpointA, kEndpointB));
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  imported = AsRecord(pair.b.sync->FindNode(node.id()));
  CHECK(imported->records.size() == 2);
  CHECK(imported->records[1] == "during-sync");
  auto const before = Observe(*imported);
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  auto const after = Observe(*AsRecord(pair.b.sync->FindNode(node.id())));
  CHECK(before.size() == after.size());
  CHECK(before[1].id == after[1].id);
  CHECK(before[1].text == after[1].text);
  CHECK(AsRecord(pair.b.sync->FindNode(node.id()))->records.size() == 2);
}

void TestTwoNodesOneEndpoint() {
  Pair pair;
  auto node_x = MakeRecordNode(pair.a, kLocalSecret);
  auto node_y = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node_x, "x", kEndpointA, 1, 100);
  AddRecord(*node_y, "y", kEndpointA, 1, 200);
  auto const op_x = Offer(pair, node_x, ShareAccess::ReadWrite);
  auto const op_y = Offer(pair, node_y, ShareAccess::ReadWrite);
  CHECK(op_x != op_y);
  CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) == 2);
  CHECK(pair.network.DeferNext(kEndpointA, kEndpointB));
  CHECK(pair.network.DuplicateNext(kEndpointA, kEndpointB));
  auto world = pair.world({kLocalSecret});
  PumpUntil(
      world,
      [&] {
        return FullyJoined(pair, op_x, node_x.id()) &&
               FullyJoined(pair, op_y, node_y.id());
      },
      80);
  auto bx = AsRecord(pair.b.sync->FindNode(node_x.id()));
  auto by = AsRecord(pair.b.sync->FindNode(node_y.id()));
  CHECK(bx->records.size() == 1);
  CHECK(bx->records[0] == "x");
  CHECK(by->records[0] == "y");
  CHECK(bx.id() != by.id());
  AddRecord(*AsRecord(pair.a.sync->FindNode(node_x.id())), "x2", kEndpointA, 2,
            300);
  AddRecord(*AsRecord(pair.b.sync->FindNode(node_y.id())), "y2", kEndpointB, 1,
            400);
  PumpUntil(
      world,
      [&] {
        return AsRecord(pair.b.sync->FindNode(node_x.id()))->records.size() ==
                   2 &&
               AsRecord(pair.a.sync->FindNode(node_y.id()))->records.size() ==
                   2;
      },
      80);
  CHECK(AsRecord(pair.b.sync->FindNode(node_x.id()))->records[1] == "x2");
  CHECK(AsRecord(pair.a.sync->FindNode(node_y.id()))->records[1] == "y2");
  CHECK(AsRecord(pair.b.sync->FindNode(node_y.id()))->records[0] == "y");
}

void TestCounterOffers() {
  Pair pair;
  auto node_x = MakeRecordNode(pair.a, kLocalSecret);
  auto node_y = MakeRecordNode(pair.b, kLocalSecret);
  AddRecord(*node_x, "from-a", kEndpointA, 1, 100);
  AddRecord(*node_y, "from-b", kEndpointB, 1, 200);
  auto const op_x = Offer(pair, node_x, ShareAccess::ReadWrite);
  auto remote_a = MakeMemoryLink(*pair.b.domain, pair.a.endpoint());
  auto const op_y =
      pair.b.sync->OfferNode(node_y, remote_a, ShareAccess::ReadWrite);
  auto world = pair.world({kLocalSecret});
  PumpUntil(
      world,
      [&] {
        return FullyJoined(pair, op_x, node_x.id()) &&
               pair.a.sync->OfferPhase(op_y) == ShareOfferPhase::Bound &&
               pair.b.sync->OfferPhase(op_y) == ShareOfferPhase::Complete &&
               pair.a.sync->FindNode(node_y.id()).is_valid();
      },
      80);
  CHECK(AsRecord(pair.b.sync->FindNode(node_x.id()))->records[0] == "from-a");
  CHECK(AsRecord(pair.a.sync->FindNode(node_y.id()))->records[0] == "from-b");
  AddRecord(*AsRecord(pair.b.sync->FindNode(node_x.id())), "b-on-x",
            kEndpointB, 1, 300);
  AddRecord(*AsRecord(pair.a.sync->FindNode(node_y.id())), "a-on-y",
            kEndpointA, 1, 400);
  PumpUntil(
      world,
      [&] {
        return AsRecord(pair.a.sync->FindNode(node_x.id()))->records.size() ==
                   2 &&
               AsRecord(pair.b.sync->FindNode(node_y.id()))->records.size() ==
                   2;
      },
      80);
  ExpectSameSharedState(pair.a.sync->FindNode(node_x.id()),
                        pair.b.sync->FindNode(node_x.id()));
  ExpectSameSharedState(pair.a.sync->FindNode(node_y.id()),
                        pair.b.sync->FindNode(node_y.id()));
}

void TestThreeReplicasDoNotMix() {
  MemoryNetwork network;
  Replica a{network, kEndpointA};
  Replica b{network, kEndpointB};
  Replica c{network, kEndpointC};
  a.Start();
  b.Start();
  c.Start();
  auto node_x = MakeRecordNode(a, kLocalSecret);
  auto node_y = MakeRecordNode(a, kLocalSecret);
  auto node_z = MakeRecordNode(a, kLocalSecret);
  AddRecord(*node_x, "x-only", kEndpointA, 1, 100);
  AddRecord(*node_y, "y-only", kEndpointA, 1, 200);
  AddRecord(*node_z, kPrivateMarker, kEndpointA, 1, 300);
  auto const op_x =
      a.sync->OfferNode(node_x, MakeMemoryLink(*a.domain, kEndpointB),
                        ShareAccess::ReadWrite);
  auto const op_y =
      a.sync->OfferNode(node_y, MakeMemoryLink(*a.domain, kEndpointC),
                        ShareAccess::ReadWrite);
  World world{.network = &network,
              .replicas = {&a, &b, &c},
              .forbidden = {kLocalSecret, kPrivateMarker}};
  PumpUntil(
      world,
      [&] {
        return PhaseIs(a, op_x, ShareOfferPhase::Complete) &&
               PhaseIs(b, op_x, ShareOfferPhase::Bound) &&
               PhaseIs(a, op_y, ShareOfferPhase::Complete) &&
               PhaseIs(c, op_y, ShareOfferPhase::Bound);
      },
      80);
  CHECK(b.sync->FindNode(node_x.id()).is_valid());
  CHECK(!b.sync->FindNode(node_y.id()).is_valid());
  CHECK(!b.domain->Find(node_z.id()));
  CHECK(b.storage.Enumerate(node_y.id()).empty());
  CHECK(b.storage.Enumerate(node_z.id()).empty());
  CHECK(c.sync->FindNode(node_y.id()).is_valid());
  CHECK(!c.sync->FindNode(node_x.id()).is_valid());
  CHECK(c.storage.Enumerate(node_x.id()).empty());
  CHECK(c.storage.Enumerate(node_z.id()).empty());
  CHECK(AsRecord(b.sync->FindNode(node_x.id()))->records[0] == "x-only");
  CHECK(AsRecord(c.sync->FindNode(node_y.id()))->records[0] == "y-only");
}

void FinishJoin(Pair& pair, ae::ObjId operation, ae::ObjId node_id) {
  auto world = pair.world({kLocalSecret});
  PumpUntil(world,
            [&] { return FullyJoined(pair, operation, node_id); }, 80);
}

void TestLossDuplicateReorderAndPartition() {
  {
    Pair pair;
    auto node = MakeRecordNode(pair.a, kLocalSecret);
    AddRecord(*node, "seed", kEndpointA, 1, 100);
    auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
    CHECK(pair.network.DropNext(kEndpointA, kEndpointB));
    ServiceRetry(pair.a, pair.network, kEndpointB);
    CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) == 1);
    FinishJoin(pair, operation, node.id());
  }
  {
    Pair pair;
    auto node = MakeRecordNode(pair.a, kLocalSecret);
    AddRecord(*node, "seed", kEndpointA, 1, 100);
    auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
    CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(pair.network.DropNext(kEndpointB, kEndpointA));
    ServiceRetry(pair.a, pair.network, kEndpointB);
    CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(pair.network.PendingCount(kEndpointB, kEndpointA) == 1);
    FinishJoin(pair, operation, node.id());
  }
  {
    Pair pair;
    auto node = MakeRecordNode(pair.a, kLocalSecret);
    AddRecord(*node, "seed", kEndpointA, 1, 100);
    auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
    CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
    CHECK(pair.network.DropNext(kEndpointA, kEndpointB));
    ServiceRetry(pair.a, pair.network, kEndpointB);
    CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) == 1);
    FinishJoin(pair, operation, node.id());
  }
  {
    Pair pair;
    auto node = MakeRecordNode(pair.a, kLocalSecret);
    AddRecord(*node, "seed", kEndpointA, 1, 100);
    auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
    CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
    CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(pair.network.DropNext(kEndpointB, kEndpointA));
    ServiceRetry(pair.a, pair.network, kEndpointB);
    CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
    CHECK(FullyJoined(pair, operation, node.id()));
    CHECK(AsRecord(pair.b.sync->FindNode(node.id()))->records.size() == 1);
  }
  {
    Pair pair;
    auto node = MakeRecordNode(pair.a, kLocalSecret);
    AddRecord(*node, "seed", kEndpointA, 1, 100);
    auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
    FinishJoin(pair, operation, node.id());
    AddRecord(*AsRecord(pair.a.sync->FindNode(node.id())), "later", kEndpointA,
              2, 500);
    pair.a.sync->Service(0);
    CHECK(pair.network.DropNext(kEndpointA, kEndpointB));
    ServiceRetry(pair.a, pair.network, kEndpointB);
    CHECK(pair.network.DuplicateNext(kEndpointA, kEndpointB));
    CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(AsRecord(pair.b.sync->FindNode(node.id()))->records.size() == 2);
    CHECK(pair.network.DropNext(kEndpointB, kEndpointA));
    ServiceRetry(pair.a, pair.network, kEndpointB);
    CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
    CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
    CHECK(AsRecord(pair.b.sync->FindNode(node.id()))->records.size() == 2);
    auto const seen = Observe(*AsRecord(pair.b.sync->FindNode(node.id())));
    CHECK(seen.size() == 2);
    CHECK(seen[1].text == "later");
    CHECK(seen[1].id.origin_uid == kEndpointA);
    CHECK(seen[1].id.origin_sequence == 2);
  }
  {
    Pair pair;
    auto node = MakeRecordNode(pair.a, kLocalSecret);
    AddRecord(*node, "seed", kEndpointA, 1, 100);
    auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
    FinishJoin(pair, operation, node.id());
    pair.network.Disconnect(kEndpointA, kEndpointB);
    AddRecord(*AsRecord(pair.a.sync->FindNode(node.id())), "after-outage",
              kEndpointA, 2, 600);
    pair.a.sync->Service(0);
    CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) == 0);
    pair.network.Reconnect(kEndpointA, kEndpointB);
    ServiceRetry(pair.a, pair.network, kEndpointB);
    auto world = pair.world({kLocalSecret});
    PumpUntil(
        world,
        [&] {
          return AsRecord(pair.b.sync->FindNode(node.id()))->records.size() ==
                 2;
        },
        40);
    CHECK(AsRecord(pair.b.sync->FindNode(node.id()))->records[1] ==
          "after-outage");
    (void)operation;
  }
}

void TestRestartDuringOffer() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 100);
  auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
  CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) == 1);
  auto const node_id = node.id();
  node = {};
  Restart(pair.a);
  pair.network.ClearQueues();
  CHECK(pair.a.sync->FindNode(node_id).is_valid());
  CHECK(PhaseIs(pair.a, operation, ShareOfferPhase::Pending));
  pair.a.sync->Service(0);
  CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) == 1);
  FinishJoin(pair, operation, node_id);
  AddRecord(*AsRecord(pair.a.sync->FindNode(node_id)), "after-restart",
            kEndpointA, 2, 700);
  auto world = pair.world({kLocalSecret});
  PumpUntil(
      world,
      [&] {
        auto b = pair.b.sync->FindNode(node_id);
        return b.is_valid() &&
               AsRecord(b)->records.size() == 2 &&
               AsRecord(b)->records[1] == "after-restart";
      },
      40);
}

void TestRestartAfterSnapshotBeforeAck() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 100);
  auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(pair.b.sync->FindNode(node.id()).is_valid());
  CHECK(PhaseIs(pair.b, operation, ShareOfferPhase::Bound));
  CHECK(PhaseIs(pair.a, operation, ShareOfferPhase::Accepted));
  CHECK(pair.network.PendingCount(kEndpointB, kEndpointA) == 1);
  Restart(pair.b);
  pair.network.ClearQueues();
  CHECK(pair.b.sync->FindNode(node.id()).is_valid());
  CHECK(AsRecord(pair.b.sync->FindNode(node.id()))->records[0] == "seed");
  ServiceRetry(pair.a, pair.network, kEndpointB);
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(FullyJoined(pair, operation, node.id()));
  CHECK(pair.b.binds == 0);
  auto binding = JoinBinding::ptr::Declare(
      ae::CreateWith{*pair.b.domain}.with_id(kJoinBindingId));
  binding.Load();
  CHECK(binding.is_loaded());
  CHECK(binding->node_ids.size() == 1);
  CHECK(binding->node_ids[0] == node.id().id());
  CHECK(AsRecord(pair.b.sync->FindNode(node.id()))->records.size() == 1);
  AddRecord(*AsRecord(pair.a.sync->FindNode(node.id())), "post-ack-restart",
            kEndpointA, 2, 800);
  auto world = pair.world({kLocalSecret});
  PumpUntil(
      world,
      [&] {
        return AsRecord(pair.b.sync->FindNode(node.id()))->records.size() ==
                   2 &&
               AsRecord(pair.b.sync->FindNode(node.id()))->records[1] ==
                   "post-ack-restart";
      },
      40);
}

void TestRestartWithUnackedEvent() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 100);
  auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
  FinishJoin(pair, operation, node.id());
  AddRecord(*AsRecord(pair.a.sync->FindNode(node.id())), "unacked", kEndpointA,
            2, 900);
  pair.a.sync->Service(0);
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(AsRecord(pair.b.sync->FindNode(node.id()))->records.size() == 2);
  CHECK(pair.network.PendingCount(kEndpointB, kEndpointA) == 1);
  auto const node_id = node.id();
  node = {};
  Restart(pair.a);
  pair.network.ClearQueues();
  CHECK(pair.a.sync->FindNode(node_id).is_valid());
  pair.a.sync->Service(0);
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(AsRecord(pair.b.sync->FindNode(node_id))->records.size() == 2);
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  AddRecord(*AsRecord(pair.a.sync->FindNode(node_id)), "fresh", kEndpointA, 3,
            950);
  auto world = pair.world({kLocalSecret});
  PumpUntil(
      world,
      [&] {
        auto b = AsRecord(pair.b.sync->FindNode(node_id));
        return b->records.size() == 3 && b->records[2] == "fresh";
      },
      40);
  auto const seen = Observe(*AsRecord(pair.b.sync->FindNode(node_id)));
  CHECK(seen.size() == 3);
  CHECK(seen[1].text == "unacked");
  CHECK(seen[1].id.origin_sequence == 2);
  CHECK(seen[2].id.origin_sequence == 3);
}

void TestRejectionsAndIsolation() {
  {
    Pair pair;
    pair.b.sync->SetShareOfferPolicy(
        [](SharedSyncRuntime::ShareOfferView const&) { return false; });
    auto node = MakeRecordNode(pair.a, kLocalSecret);
    AddRecord(*node, "nope", kEndpointA, 1, 100);
    auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
    auto world = pair.world({kLocalSecret});
    PumpUntil(
        world,
        [&] {
          return PhaseIs(pair.a, operation, ShareOfferPhase::Rejected) &&
                 PhaseIs(pair.b, operation, ShareOfferPhase::Rejected);
        },
        40);
    CHECK(!pair.b.sync->FindNode(node.id()).is_valid());
    CHECK(pair.b.storage.Enumerate(node.id()).empty());
    CHECK(!pair.b.domain->Find(node.id()));
  }
  {
    Pair pair;
    auto node = MakeRecordNode(pair.a, kLocalSecret);
    auto other = MakeRecordNode(pair.a, kLocalSecret);
    AddRecord(*other, "wrong-node", kEndpointA, 1, 100);
    pair.b.sync->SetShareOfferPolicy(
        [&](SharedSyncRuntime::ShareOfferView const& offer) {
          return offer.node_id == node.id();
        });
    auto const operation = Offer(pair, other, ShareAccess::ReadWrite);
    auto world = pair.world({kLocalSecret});
    PumpUntil(world,
              [&] {
                return PhaseIs(pair.a, operation, ShareOfferPhase::Rejected);
              },
              20);
    CHECK(!pair.b.sync->FindNode(other.id()).is_valid());
    CHECK(pair.b.storage.Enumerate(other.id()).empty());
  }
  {
    Pair pair;
    auto node = JoinOtherNode::ptr::Create(ae::CreateWith{*pair.a.domain});
    InitializeRuntimeNode(*node);
    node->tag = 7;
    node->AddShare(MakeMemoryLink(*pair.a.domain, pair.a.endpoint()),
                   ShareAccess::ReadWrite);
    SaveSync(node);
    pair.a.sync->RegisterNode(node);
    pair.b.sync->SetShareOfferPolicy(
        [](SharedSyncRuntime::ShareOfferView const& offer) {
          return offer.root_class_id == JoinRecordNode::kClassId;
        });
    auto const operation = pair.a.sync->OfferNode(
        node, MakeMemoryLink(*pair.a.domain, pair.b.endpoint()),
        ShareAccess::ReadWrite);
    auto world = pair.world();
    PumpUntil(world,
              [&] {
                return PhaseIs(pair.a, operation, ShareOfferPhase::Rejected);
              },
              20);
    CHECK(!pair.b.domain->Find(node.id()));
    CHECK(pair.b.storage.Enumerate(node.id()).empty());
    CHECK(PhaseIs(pair.b, operation, ShareOfferPhase::Rejected));
  }
}

void TestWrongSourceAndCorrupt() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 100);
  auto const operation = Offer(pair, node, ShareAccess::ReadWrite);
  CHECK(pair.network.CorruptNext(kEndpointA, kEndpointB));
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(pair.b.sync->OfferStatuses().empty());
  CHECK(!pair.b.sync->FindNode(node.id()).is_valid());
  ServiceRetry(pair.a, pair.network, kEndpointB);
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  auto snapshot = pair.network.PeekNext(kEndpointA, kEndpointB);
  struct Eve {
    static void OnBytes(void*, std::string const&,
                        std::vector<std::uint8_t> const&) {}
  };
  MemoryTransport eve{pair.network, "endpoint-eve"};
  eve.BindReceive(nullptr, &Eve::OnBytes);
  eve.Send(kEndpointB, snapshot);
  CHECK(pair.network.DeliverNext("endpoint-eve", kEndpointB));
  CHECK(!pair.b.sync->FindNode(node.id()).is_valid());
  CHECK(pair.network.CorruptNext(kEndpointA, kEndpointB));
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(!pair.b.sync->FindNode(node.id()).is_valid());
  ServiceRetry(pair.a, pair.network, kEndpointB);
  FinishJoin(pair, operation, node.id());
  CHECK(AsRecord(pair.b.sync->FindNode(node.id()))->records[0] == "seed");
  CHECK(!AsRecord(pair.b.sync->FindNode(node.id()))->local_secret.is_valid());
}

void TestReadOnlyCannotWrite() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 100);
  auto const operation = Offer(pair, node, ShareAccess::ReadOnly);
  auto world = pair.world({kLocalSecret});
  PumpUntil(world, [&] { return FullyJoined(pair, operation, node.id()); }, 40);
  auto a_node = AsRecord(pair.a.sync->FindNode(node.id()));
  auto b_node = AsRecord(pair.b.sync->FindNode(node.id()));
  CHECK(a_node->shares[1].GetAccess() == ShareAccess::ReadOnly);
  CHECK(b_node->shares[1].GetAccess() == ShareAccess::ReadOnly);
  AddRecord(*a_node, "from-writer", kEndpointA, 2, 500);
  PumpUntil(
      world,
      [&] {
        return AsRecord(pair.b.sync->FindNode(node.id()))->records.size() == 2;
      },
      40);
  CHECK(AsRecord(pair.b.sync->FindNode(node.id()))->records[1] ==
        "from-writer");
  auto const before = Observe(*AsRecord(pair.a.sync->FindNode(node.id())));
  AddRecord(*AsRecord(pair.b.sync->FindNode(node.id())), "readonly-write",
            kEndpointB, 1, 800);
  pair.b.sync->Service(0);
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  auto const after = Observe(*AsRecord(pair.a.sync->FindNode(node.id())));
  CHECK(after.size() == before.size());
  CHECK(after[0].id == before[0].id);
  CHECK(after[1].text == "from-writer");
  bool leaked = false;
  for (auto const& record : after) {
    if (record.text == "readonly-write") {
      leaked = true;
    }
  }
  CHECK(!leaked);
  CHECK(PhaseIs(pair.a, operation, ShareOfferPhase::Complete));
  CHECK(PhaseIs(pair.b, operation, ShareOfferPhase::Bound));
  AddRecord(*AsRecord(pair.a.sync->FindNode(node.id())), "still-flows",
            kEndpointA, 3, 900);
  PumpUntil(
      world,
      [&] {
        auto const seen =
            Observe(*AsRecord(pair.b.sync->FindNode(node.id())));
        for (auto const& record : seen) {
          if (record.text == "still-flows" &&
              record.id.origin_uid == kEndpointA &&
              record.id.origin_sequence == 3) {
            return true;
          }
        }
        return false;
      },
      40);
  CHECK(AsRecord(pair.a.sync->FindNode(node.id()))->records.size() == 3);
}

bool RequestJoined(Replica& holder, Replica& requester, ae::ObjId operation,
                   ae::ObjId node_id) {
  return PhaseIs(holder, operation, ShareOfferPhase::Complete) &&
         PhaseIs(requester, operation, ShareOfferPhase::Bound) &&
         requester.sync->FindNode(node_id).is_valid();
}

void TestRequestJoinDeferredThenNewAttempt() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 100);
  pair.a.sync->SetShareOfferPolicy({});
  auto const operation =
      pair.b.sync->RequestJoin(kEndpointA, node.id(), ShareAccess::ReadWrite);
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(PhaseIs(pair.a, operation, ShareOfferPhase::AwaitingDecision));
  CHECK(!pair.b.sync->FindNode(node.id()).is_valid());
  for (int i = 0; i < 3; ++i) {
    pair.a.sync->Service(static_cast<std::uint64_t>(i));
    pair.b.sync->Service(static_cast<std::uint64_t>(i));
  }
  CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) == 0);
  CHECK(PhaseIs(pair.a, operation, ShareOfferPhase::AwaitingDecision));
  pair.a.sync->RejectJoin(operation);
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(PhaseIs(pair.a, operation, ShareOfferPhase::Rejected));
  CHECK(PhaseIs(pair.b, operation, ShareOfferPhase::Rejected));
  CHECK(!pair.b.domain->Find(node.id()));
  g_now += 4 * kShareOfferRetryIntervalUs;
  pair.a.sync->Service(g_now);
  pair.b.sync->Service(g_now);
  CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) == 0);
  CHECK(pair.network.PendingCount(kEndpointB, kEndpointA) == 0);

  auto const again =
      pair.b.sync->RequestJoin(kEndpointA, node.id(), ShareAccess::ReadWrite);
  CHECK(again != operation);
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(PhaseIs(pair.a, again, ShareOfferPhase::AwaitingDecision));
  pair.a.sync->AcceptJoin(again, ShareAccess::ReadWrite,
                          MakeMemoryLink(*pair.a.domain, kEndpointB));
  auto world = pair.world({kLocalSecret});
  PumpUntil(
      world, [&] { return RequestJoined(pair.a, pair.b, again, node.id()); },
      40);
  AddRecord(*AsRecord(pair.a.sync->FindNode(node.id())), "from-a", kEndpointA,
            2, 200);
  AddRecord(*AsRecord(pair.b.sync->FindNode(node.id())), "from-b", kEndpointB,
            1, 300);
  PumpUntil(
      world,
      [&] {
        auto b = AsRecord(pair.b.sync->FindNode(node.id()));
        return b->records.size() == 3 && b->records[2] == "from-b";
      },
      40);
  ExpectSameSharedState(pair.a.sync->FindNode(node.id()),
                        pair.b.sync->FindNode(node.id()));
}

void TestRequestTwoNodesAndThirdParticipant() {
  {
    Pair pair;
    auto node_x = MakeRecordNode(pair.a, kLocalSecret);
    auto node_y = MakeRecordNode(pair.a, kLocalSecret);
    AddRecord(*node_x, "x-only", kEndpointA, 1, 100);
    AddRecord(*node_y, "y-only", kEndpointA, 1, 110);
    auto const op_x = pair.b.sync->RequestJoin(kEndpointA, node_x.id(),
                                               ShareAccess::ReadWrite);
    auto const op_y = pair.b.sync->RequestJoin(kEndpointA, node_y.id(),
                                               ShareAccess::ReadWrite);
    auto world = pair.world({kLocalSecret});
    PumpUntil(
        world,
        [&] {
          return RequestJoined(pair.a, pair.b, op_x, node_x.id()) &&
                 RequestJoined(pair.a, pair.b, op_y, node_y.id());
        },
        80);
    CHECK(AsRecord(pair.b.sync->FindNode(node_x.id()))->records[0] == "x-only");
    CHECK(AsRecord(pair.b.sync->FindNode(node_y.id()))->records[0] == "y-only");
    CHECK(AsRecord(pair.b.sync->FindNode(node_x.id()))->records.size() == 1);
  }
  {
    MemoryNetwork network;
    Replica a{network, kEndpointA};
    Replica b{network, kEndpointB};
    Replica c{network, kEndpointC};
    a.Start();
    b.Start();
    c.Start();
    auto node = MakeRecordNode(a, kLocalSecret);
    auto hidden = MakeRecordNode(a, kLocalSecret);
    AddRecord(*node, "shared", kEndpointA, 1, 100);
    AddRecord(*hidden, kPrivateMarker, kEndpointA, 1, 50);
    auto const to_b = a.sync->OfferNode(
        node, MakeMemoryLink(*a.domain, kEndpointB), ShareAccess::ReadWrite);
    World world{.network = &network,
                .replicas = {&a, &b, &c},
                .forbidden = {kLocalSecret, kPrivateMarker}};
    PumpUntil(
        world,
        [&] {
          return PhaseIs(a, to_b, ShareOfferPhase::Complete) &&
                 PhaseIs(b, to_b, ShareOfferPhase::Bound) &&
                 b.sync->FindNode(node.id()).is_valid();
        },
        40);
    auto const to_c =
        c.sync->RequestJoin(kEndpointA, node.id(), ShareAccess::ReadWrite);
    PumpUntil(
        world,
        [&] {
          return RequestJoined(a, c, to_c, node.id()) &&
                 b.sync->FindNode(node.id()).is_valid() &&
                 !c.sync->FindNode(hidden.id()).is_valid() &&
                 c.storage.Enumerate(hidden.id()).empty();
        },
        80);
    CHECK(c.sync->FindNode(node.id()).id() == node.id());
    CHECK(AsRecord(c.sync->FindNode(node.id()))->records[0] == "shared");
    AddRecord(*AsRecord(a.sync->FindNode(node.id())), "to-both", kEndpointA, 2,
              500);
    PumpUntil(
        world,
        [&] {
          auto seen_b = Observe(*AsRecord(b.sync->FindNode(node.id())));
          auto seen_c = Observe(*AsRecord(c.sync->FindNode(node.id())));
          return seen_b.size() == 2 && seen_c.size() == 2 &&
                 seen_b[1].id == seen_c[1].id &&
                 seen_b[1].text == "to-both" && seen_c[1].text == "to-both";
        },
        40);
    AddRecord(*AsRecord(b.sync->FindNode(node.id())), "from-b", kEndpointB, 1,
              700);
    PumpUntil(
        world,
        [&] {
          auto seen_c = Observe(*AsRecord(c.sync->FindNode(node.id())));
          return seen_c.size() == 3 && seen_c[2].text == "from-b" &&
                 seen_c[2].id.origin_uid == kEndpointB;
        },
        40);
    CHECK(!c.sync->FindNode(hidden.id()).is_valid());
  }
}

void TestRequestLossAndQuiet() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 100);
  auto const operation =
      pair.b.sync->RequestJoin(kEndpointA, node.id(), ShareAccess::ReadWrite);
  CHECK(pair.network.DropNext(kEndpointB, kEndpointA));
  ServiceRetry(pair.b, pair.network, kEndpointA);
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(pair.network.DropNext(kEndpointA, kEndpointB));
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(!pair.b.sync->FindNode(node.id()).is_valid());
  ServiceRetry(pair.b, pair.network, kEndpointA);
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(PhaseIs(pair.b, operation, ShareOfferPhase::Admitted));
  ServiceRetry(pair.a, pair.network, kEndpointB);
  CHECK(pair.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(RequestJoined(pair.a, pair.b, operation, node.id()));
  auto world = pair.world();
  Drain(world);
  auto const queued = pair.network.PendingCount(kEndpointA, kEndpointB) +
                      pair.network.PendingCount(kEndpointB, kEndpointA);
  g_now += 5 * kShareOfferRetryIntervalUs;
  pair.a.sync->Service(g_now);
  pair.b.sync->Service(g_now);
  CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) +
            pair.network.PendingCount(kEndpointB, kEndpointA) ==
        queued);
}

void TestRequestRestartFromStorage() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 100);
  pair.a.sync->SetShareOfferPolicy({});
  auto const operation =
      pair.b.sync->RequestJoin(kEndpointA, node.id(), ShareAccess::ReadWrite);
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(PhaseIs(pair.a, operation, ShareOfferPhase::AwaitingDecision));
  auto const node_id = node.id();
  node = {};
  Restart(pair.a);
  pair.network.ClearQueues();
  CHECK(pair.a.sync->FindNode(node_id).is_valid());
  CHECK(PhaseIs(pair.a, operation, ShareOfferPhase::AwaitingDecision));
  CHECK(!pair.a.sync->OfferStatuses().empty());
  pair.a.sync->AcceptJoin(operation, ShareAccess::ReadWrite,
                          MakeMemoryLink(*pair.a.domain, kEndpointB));
  auto world = pair.world({kLocalSecret});
  PumpUntil(
      world,
      [&] { return RequestJoined(pair.a, pair.b, operation, node_id); }, 40);
  AddRecord(*AsRecord(pair.a.sync->FindNode(node_id)), "after-restart",
            kEndpointA, 2, 400);
  PumpUntil(
      world,
      [&] {
        auto b = AsRecord(pair.b.sync->FindNode(node_id));
        return b->records.size() == 2 && b->records[1] == "after-restart";
      },
      40);

  Pair saved;
  auto kept = MakeRecordNode(saved.a, kLocalSecret);
  AddRecord(*kept, "seed", kEndpointA, 1, 100);
  saved.a.sync->SetShareOfferPolicy({});
  auto const op = saved.b.sync->RequestJoin(kEndpointA, kept.id(),
                                            ShareAccess::ReadWrite);
  CHECK(saved.network.DeliverNext(kEndpointB, kEndpointA));
  saved.a.sync->AcceptJoin(op, ShareAccess::ReadWrite,
                           MakeMemoryLink(*saved.a.domain, kEndpointB));
  CHECK(saved.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(saved.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(saved.b.sync->FindNode(kept.id()).is_valid());
  CHECK(PhaseIs(saved.b, op, ShareOfferPhase::Bound));
  auto const kept_id = kept.id();
  kept = {};
  Restart(saved.b);
  saved.network.ClearQueues();
  CHECK(saved.b.binds == 0);
  CHECK(saved.b.sync->FindNode(kept_id).is_valid());
  auto binding = JoinBinding::ptr::Declare(
      ae::CreateWith{*saved.b.domain}.with_id(kJoinBindingId));
  binding.Load();
  CHECK(binding.is_loaded());
  CHECK(binding->node_ids.size() == 1);
  CHECK(binding->node_ids[0] == kept_id.id());
  ServiceRetry(saved.a, saved.network, kEndpointB);
  CHECK(saved.network.DeliverNext(kEndpointA, kEndpointB));
  CHECK(saved.b.binds == 0);
  CHECK(saved.network.DeliverNext(kEndpointB, kEndpointA));
  AddRecord(*AsRecord(saved.a.sync->FindNode(kept_id)), "fresh", kEndpointA, 2,
            800);
  auto saved_world = saved.world({kLocalSecret});
  PumpUntil(
      saved_world,
      [&] {
        auto b = AsRecord(saved.b.sync->FindNode(kept_id));
        return b->records.size() == 2 && b->records[1] == "fresh";
      },
      40);
}

void TestRequestTamperedRepeat() {
  Pair pair;
  auto node = MakeRecordNode(pair.a, kLocalSecret);
  auto other = MakeRecordNode(pair.a, kLocalSecret);
  AddRecord(*node, "seed", kEndpointA, 1, 100);
  pair.a.sync->SetShareOfferPolicy({});
  auto const operation =
      pair.b.sync->RequestJoin(kEndpointA, node.id(), ShareAccess::ReadWrite);
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(PhaseIs(pair.a, operation, ShareOfferPhase::AwaitingDecision));
  ShareOfferFrame forged{
      .packet_id = ae::ObjId{9},
      .operation_id = operation,
      .target_node_id = node.id(),
      .root_class_id = 0,
      .access = static_cast<std::uint8_t>(ShareAccess::ReadOnly),
  };
  pair.b.transport->Send(kEndpointA, EncodeShareRequestFrame(forged));
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(PhaseIs(pair.a, operation, ShareOfferPhase::AwaitingDecision));
  CHECK(pair.network.PendingCount(kEndpointA, kEndpointB) == 0);
  forged.target_node_id = other.id();
  forged.access = static_cast<std::uint8_t>(ShareAccess::ReadWrite);
  pair.b.transport->Send(kEndpointA, EncodeShareRequestFrame(forged));
  CHECK(pair.network.DeliverNext(kEndpointB, kEndpointA));
  CHECK(!pair.b.sync->FindNode(other.id()).is_valid());
  CHECK(pair.a.storage.Enumerate(other.id()).size() > 0);
  struct Eve {
    static void OnBytes(void*, std::string const&,
                        std::vector<std::uint8_t> const&) {}
  };
  MemoryTransport eve{pair.network, "endpoint-eve"};
  eve.BindReceive(nullptr, &Eve::OnBytes);
  forged.target_node_id = node.id();
  eve.Send(kEndpointA, EncodeShareRequestFrame(forged));
  CHECK(pair.network.DeliverNext("endpoint-eve", kEndpointA));
  CHECK(PhaseIs(pair.a, operation, ShareOfferPhase::AwaitingDecision));
  CHECK(!pair.b.sync->FindNode(node.id()).is_valid());
  pair.a.sync->AcceptJoin(operation, ShareAccess::ReadOnly,
                          MakeMemoryLink(*pair.a.domain, kEndpointB));
  auto world = pair.world({kLocalSecret});
  PumpUntil(
      world,
      [&] { return RequestJoined(pair.a, pair.b, operation, node.id()); }, 40);
  CHECK(AsRecord(pair.b.sync->FindNode(node.id()))->shares[1].GetAccess() ==
        ShareAccess::ReadOnly);
  CHECK(!pair.b.sync->FindNode(other.id()).is_valid());
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestFrameRoundTrip();
  apptraverse::test::TestParallelEndpointExpectations();
  apptraverse::test::TestForgetOneExpectationLeavesTheOther();
  apptraverse::test::TestOfferIsIdempotent();
  apptraverse::test::TestMainExchangeAndBothSides();
  apptraverse::test::TestSimultaneousRecords();
  apptraverse::test::TestEventDuringInitialSyncAndRepeatSnapshot();
  apptraverse::test::TestTwoNodesOneEndpoint();
  apptraverse::test::TestCounterOffers();
  apptraverse::test::TestThreeReplicasDoNotMix();
  apptraverse::test::TestLossDuplicateReorderAndPartition();
  apptraverse::test::TestRestartDuringOffer();
  apptraverse::test::TestRestartAfterSnapshotBeforeAck();
  apptraverse::test::TestRestartWithUnackedEvent();
  apptraverse::test::TestRejectionsAndIsolation();
  apptraverse::test::TestWrongSourceAndCorrupt();
  apptraverse::test::TestReadOnlyCannotWrite();
  apptraverse::test::TestRequestJoinDeferredThenNewAttempt();
  apptraverse::test::TestRequestTwoNodesAndThirdParticipant();
  apptraverse::test::TestRequestLossAndQuiet();
  apptraverse::test::TestRequestRestartFromStorage();
  apptraverse::test::TestRequestTamperedRepeat();
  std::cout << "shared_node_join_test OK\n";
  return 0;
}
