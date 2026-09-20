#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
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

std::string const kA = "endpoint-a";
std::string const kB = "endpoint-b";
std::string const kC = "endpoint-c";
std::string const kSecret = "TOPO_LOCAL_SECRET_do_not_ship";

class TopoSecret : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::TopoSecret", TopoSecret, ae::Obj,
                           0)

 protected:
  TopoSecret() = default;

 public:
  explicit TopoSecret(ae::ObjProp prop) : Obj{prop} {}
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

class AddTopoEvent;
class TopoNode : public NodeFor<TopoNode, SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::TopoNode", TopoNode, SharedNode,
                           0)

 protected:
  TopoNode() = default;

 public:
  explicit TopoNode(ae::ObjProp prop) : NodeFor{prop} {}
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
  LocalPtr<TopoSecret> local_secret;
  void Apply(AddTopoEvent const& event);
};

class AddTopoEvent : public EventFor<TopoNode, AddTopoEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::AddTopoEvent", AddTopoEvent,
                           Event, 0)

 protected:
  AddTopoEvent() = default;

 public:
  explicit AddTopoEvent(ae::ObjProp prop) : EventFor{prop} {}
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

void TopoNode::Apply(AddTopoEvent const& event) {
  records.push_back(event.text);
  NoteMaterializedChange();
}

APPTRAVERSE_REGISTER(TopoSecret);
APPTRAVERSE_REGISTER(TopoNode);
APPTRAVERSE_REGISTER(AddTopoEvent);

bool AcceptAll(SharedSyncRuntime::ShareOfferView const&) { return true; }

bool Contains(std::vector<std::uint8_t> const& bytes, std::string const& needle) {
  if (needle.empty() || bytes.size() < needle.size()) {
    return false;
  }
  std::string_view const view(reinterpret_cast<char const*>(bytes.data()),
                              bytes.size());
  return view.find(needle) != std::string_view::npos;
}

MemoryLink::ptr MakeLink(ae::Domain& domain, std::string endpoint) {
  auto link = MemoryLink::ptr::Create(
      ae::CreateWith{domain}.with_id(ae::ObjId::GenerateUnique()));
  link->endpoint_uid = std::move(endpoint);
  link->heartbeat_interval_ms = 1000;
  InitializeRuntimeNode(*link);
  link.Save();
  return link;
}

class SendCounter final : public IByteTransport {
 public:
  SendCounter(MemoryTransport& inner, std::uint64_t& sends)
      : inner_{inner}, sends_{sends} {}

  std::string const& local_endpoint_uid() const override {
    return inner_.local_endpoint_uid();
  }
  void Send(std::string const& destination_endpoint,
            std::vector<std::uint8_t> bytes) override {
    ++sends_;
    inner_.Send(destination_endpoint, std::move(bytes));
  }
  void BindReceive(void* ctx, ReceiveFn fn) override {
    inner_.BindReceive(ctx, fn);
  }
  void ClearReceive() override { inner_.ClearReceive(); }
  EndpointAvailability Availability(std::string const& endpoint) const override {
    return inner_.Availability(endpoint);
  }
  void BindAvailability(void* ctx, AvailabilityFn fn) override {
    inner_.BindAvailability(ctx, fn);
  }
  void ClearAvailability() override { inner_.ClearAvailability(); }

 private:
  MemoryTransport& inner_;
  std::uint64_t& sends_;
};

struct Replica {
  Replica(MemoryNetwork& network, std::string endpoint)
      : storage{}, network_{network}, endpoint_{std::move(endpoint)} {}

  void Start() {
    domain = std::make_unique<ae::Domain>(storage);
    transport = std::make_unique<MemoryTransport>(network_, endpoint_);
    counter = std::make_unique<SendCounter>(*transport, sends);
    sync = std::make_unique<SharedSyncRuntime>(*domain, storage, *counter);
    sync->AllowStandaloneEventClass(AddTopoEvent::kClassId);
    sync->SetShareOfferPolicy(AcceptAll);
    sync->SetLinkForEndpoint([this](std::string const& endpoint) {
      return MakeLink(*domain, endpoint);
    });
    sync->SetInitialNodeImportedCallback(
        [](std::string const&, SharedNode::ptr) { return true; });
  }

  void Stop() {
    sync.reset();
    counter.reset();
    transport.reset();
    domain.reset();
  }

  std::string const& endpoint() const { return endpoint_; }

  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  std::unique_ptr<MemoryTransport> transport;
  std::unique_ptr<SendCounter> counter;
  std::unique_ptr<SharedSyncRuntime> sync;
  std::uint64_t sends{0};

 private:
  MemoryNetwork& network_;
  std::string endpoint_;
};

struct World {
  MemoryNetwork* network{nullptr};
  std::vector<Replica*> replicas;
};

bool DeliverRound(World& world, std::vector<std::uint8_t>* saved_add) {
  bool any = false;
  for (auto* from : world.replicas) {
    for (auto* to : world.replicas) {
      if (from == to) {
        continue;
      }
      auto const& bytes =
          world.network->PeekNext(from->endpoint(), to->endpoint());
      if (Contains(bytes, kSecret)) {
        std::cerr << "secret leaked\n";
        CHECK(false);
      }
      if (saved_add != nullptr && saved_add->empty() &&
          from->endpoint() == kA && to->endpoint() == kB && !bytes.empty()) {
        EventFrame frame;
        if (DecodeEventFrame(bytes, frame) &&
            frame.event_class_id == AddShareEvent::kClassId) {
          *saved_add = bytes;
        }
      }
      if (world.network->DeliverNext(from->endpoint(), to->endpoint())) {
        any = true;
      }
    }
  }
  return any;
}

std::string Diagnose(World const& world) {
  std::string out;
  for (auto* replica : world.replicas) {
    out += replica->endpoint();
    for (auto const& status : replica->sync->OfferStatuses()) {
      out += " op=";
      out += std::to_string(status.operation_id.id());
      out += " phase=";
      out += std::to_string(static_cast<int>(status.phase));
    }
    out += "\n";
  }
  return out;
}

void Pump(World& world, auto&& done, int max_steps,
          std::vector<std::uint8_t>* saved_add = nullptr) {
  std::uint64_t now = 0;
  for (int step = 0; step < max_steps; ++step) {
    if (done()) {
      return;
    }
    DeliverRound(world, saved_add);
    for (auto* replica : world.replicas) {
      replica->sync->Service(now);
    }
    if (done()) {
      return;
    }
    now += kShareOfferRetryIntervalUs;
  }
  std::cerr << "pump stopped\n" << Diagnose(world);
  CHECK(false);
}

TopoNode::ptr AsTopo(SharedNode::ptr node) {
  TopoNode::ptr concrete = node;
  concrete.Load();
  CHECK(concrete.is_loaded());
  return concrete;
}

struct Seen {
  SharedEventId id;
  std::uint64_t timestamp_us{0};
  std::string text;
};

std::vector<Seen> Observe(TopoNode const& node) {
  std::vector<Seen> out;
  for (auto const& record : node.journal) {
    if (!record.HasSharedIdentity()) {
      continue;
    }
    auto event = record.event;
    if (!event.is_loaded()) {
      event.Load();
    }
    if (event->GetClassId() != AddTopoEvent::kClassId) {
      continue;
    }
    AddTopoEvent::ptr concrete = event;
    CHECK(concrete.is_loaded());
    out.push_back(Seen{.id = record.identity,
                       .timestamp_us = record.order.timestamp_us,
                       .text = concrete->text});
  }
  std::sort(out.begin(), out.end(), [](Seen const& a, Seen const& b) {
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

struct ShareView {
  ae::ObjId share_id;
  ae::ObjId link_id;
  std::string endpoint;
  ShareAccess access{ShareAccess::ReadWrite};
};

std::vector<ShareView> SharesOf(SharedNode::ptr node) {
  std::vector<ShareView> out;
  for (auto const& share : node->shares) {
    if (!share.link.is_loaded()) {
      share.link.Load();
    }
    CHECK(share.link.is_loaded());
    CHECK(share.share_id.is_valid());
    out.push_back(ShareView{.share_id = share.share_id,
                            .link_id = share.link.id(),
                            .endpoint = share.link->EndpointUid(),
                            .access = share.GetAccess()});
  }
  std::sort(out.begin(), out.end(), [](ShareView const& a, ShareView const& b) {
    return a.endpoint < b.endpoint;
  });
  return out;
}

ShareView const* FindEndpoint(std::vector<ShareView> const& shares,
                              std::string const& endpoint) {
  for (auto const& share : shares) {
    if (share.endpoint == endpoint) {
      return &share;
    }
  }
  return nullptr;
}

ae::ObjId AddEventObjectId(SharedNode::ptr node, ae::ObjId share_id) {
  for (auto const& record : node->journal) {
    auto event = record.event;
    if (!event.is_valid()) {
      continue;
    }
    if (!event.is_loaded()) {
      event.Load();
    }
    if (event->GetClassId() != AddShareEvent::kClassId) {
      continue;
    }
    auto const& add = static_cast<AddShareEvent const&>(*event);
    if (add.share_id == share_id) {
      return event.id();
    }
  }
  return {};
}

void ExpectSameRecords(SharedNode::ptr left, SharedNode::ptr right) {
  auto const a = Observe(*AsTopo(left));
  auto const b = Observe(*AsTopo(right));
  CHECK(a.size() == b.size());
  CHECK(AsTopo(left)->records.size() == a.size());
  CHECK(AsTopo(right)->records.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    CHECK(a[i].id == b[i].id);
    CHECK(a[i].timestamp_us == b[i].timestamp_us);
    CHECK(a[i].text == b[i].text);
    CHECK(AsTopo(left)->records[i] == a[i].text);
    CHECK(AsTopo(right)->records[i] == b[i].text);
  }
}

void ExpectSameShares(SharedNode::ptr left, SharedNode::ptr right) {
  auto const a = SharesOf(left);
  auto const b = SharesOf(right);
  CHECK(a.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    CHECK(a[i].share_id == b[i].share_id);
    CHECK(a[i].link_id == b[i].link_id);
    CHECK(a[i].endpoint == b[i].endpoint);
    CHECK(a[i].access == b[i].access);
  }
}

bool HasText(SharedNode::ptr node, std::string const& text,
             std::string const& origin, std::uint64_t sequence) {
  for (auto const& seen : Observe(*AsTopo(node))) {
    if (seen.text == text && seen.id.origin_uid == origin &&
        seen.id.origin_sequence == sequence) {
      return true;
    }
  }
  return false;
}

std::uint64_t NextStamp(SharedNode const& node) {
  std::uint64_t stamp = 1;
  for (auto const& record : node.journal) {
    if (record.order.timestamp_us >= stamp) {
      stamp = record.order.timestamp_us + 1;
    }
  }
  return stamp;
}

std::uint64_t g_stamp = 0;

void AddRecord(TopoNode& node, std::string text, std::string origin,
               std::uint64_t sequence) {
  auto const local = NextStamp(node);
  if (g_stamp < local) {
    g_stamp = local;
  } else {
    ++g_stamp;
  }
  auto event = AddTopoEvent::ptr::Create(ae::CreateWith{*node.domain});
  event->text = std::move(text);
  node.CommitShared(std::move(event),
                    SharedEventId{.origin_uid = std::move(origin),
                                  .origin_sequence = sequence},
                    SharedEventOrder{.timestamp_us = g_stamp});
  TopoNode::ptr::MakeFromThis(&node).Save();
}

struct Trio {
  MemoryNetwork network;
  Replica a;
  Replica b;
  Replica c;
  Trio() : a(network, kA), b(network, kB), c(network, kC) {
    a.Start();
    b.Start();
    c.Start();
  }
  World world() { return World{&network, {&a, &b, &c}}; }
};

TopoNode::ptr MakeNode(Replica& replica) {
  auto node = TopoNode::ptr::Create(ae::CreateWith{*replica.domain});
  InitializeRuntimeNode(*node);
  auto hidden = TopoSecret::ptr::Create(ae::CreateWith{*replica.domain});
  hidden->mark = kSecret;
  hidden.Save();
  node->local_secret = hidden;
  auto self = MakeLink(*replica.domain, replica.endpoint());
  node->AddShare(self, ShareAccess::ReadWrite);
  node.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }
  replica.sync->RegisterNode(node);
  return node;
}

bool RemotePhaseComplete(SharedNode::ptr node, std::string const& endpoint) {
  for (auto const& share : node->shares) {
    if (!share.link.is_loaded()) {
      share.link.Load();
    }
    if (share.link->EndpointUid() != endpoint) {
      continue;
    }
    auto const index = node->FindLinkSyncIndexForShare(share.share_id);
    if (index >= node->link_sync_states.size()) {
      return false;
    }
    auto state = node->link_sync_states[index];
    if (!state.is_loaded()) {
      state.Load();
    }
    return state->GetInitialSyncPhase() == InitialSyncPhase::Complete;
  }
  return false;
}

bool Joined(Replica& holder, Replica& peer, ae::ObjId operation,
            ae::ObjId node_id) {
  return holder.sync->OfferPhase(operation) == ShareOfferPhase::Complete &&
         peer.sync->OfferPhase(operation) == ShareOfferPhase::Bound &&
         peer.sync->FindNode(node_id).is_valid();
}

bool ThreeWay(Trio& trio, ae::ObjId node_id) {
  auto na = trio.a.sync->FindNode(node_id);
  auto nb = trio.b.sync->FindNode(node_id);
  auto nc = trio.c.sync->FindNode(node_id);
  if (!na.is_valid() || !nb.is_valid() || !nc.is_valid()) {
    return false;
  }
  if (na->shares.size() != 3 || nb->shares.size() != 3 ||
      nc->shares.size() != 3) {
    return false;
  }
  auto const sa = SharesOf(na);
  auto const sb = SharesOf(nb);
  auto const sc = SharesOf(nc);
  if (sa.size() != 3 || sb.size() != 3 || sc.size() != 3) {
    return false;
  }
  // operator== is not generated for ShareView. Compare fields.
  for (std::size_t i = 0; i < sa.size(); ++i) {
    if (sa[i].share_id != sb[i].share_id || sa[i].share_id != sc[i].share_id ||
        sa[i].endpoint != sb[i].endpoint || sa[i].endpoint != sc[i].endpoint ||
        sa[i].access != sb[i].access || sa[i].access != sc[i].access ||
        sa[i].link_id != sb[i].link_id || sa[i].link_id != sc[i].link_id) {
      return false;
    }
  }
  auto const records_a = Observe(*AsTopo(na));
  auto const records_b = Observe(*AsTopo(nb));
  auto const records_c = Observe(*AsTopo(nc));
  if (records_a.size() != records_b.size() ||
      records_a.size() != records_c.size()) {
    return false;
  }
  for (std::size_t i = 0; i < records_a.size(); ++i) {
    if (records_a[i].id != records_b[i].id ||
        records_a[i].id != records_c[i].id ||
        records_a[i].text != records_b[i].text ||
        records_a[i].text != records_c[i].text ||
        records_a[i].timestamp_us != records_b[i].timestamp_us ||
        records_a[i].timestamp_us != records_c[i].timestamp_us) {
      return false;
    }
  }
  if (!RemotePhaseComplete(nb, kA) || !RemotePhaseComplete(nb, kC) ||
      !RemotePhaseComplete(nc, kA) || !RemotePhaseComplete(nc, kB) ||
      !RemotePhaseComplete(na, kB) || !RemotePhaseComplete(na, kC)) {
    return false;
  }
  return true;
}

void ExpectProtocolShareId(Trio& trio, ae::ObjId node_id,
                           std::string const& endpoint) {
  auto na = trio.a.sync->FindNode(node_id);
  auto nb = trio.b.sync->FindNode(node_id);
  auto nc = trio.c.sync->FindNode(node_id);
  auto const* share = FindEndpoint(SharesOf(na), endpoint);
  CHECK(share != nullptr);
  auto const id_a = AddEventObjectId(na, share->share_id);
  auto const id_b = AddEventObjectId(nb, share->share_id);
  auto const id_c = AddEventObjectId(nc, share->share_id);
  CHECK(id_a.is_valid());
  CHECK(id_b.is_valid());
  CHECK(id_c.is_valid());
  // The publisher's event object is the protocol id. The incremental
  // importer allocates a different event object and must keep share_id.
  CHECK(id_a == share->share_id);
  CHECK(id_b != share->share_id);
  CHECK(id_c == share->share_id);
  CHECK(FindEndpoint(SharesOf(nb), endpoint)->share_id == share->share_id);
  CHECK(FindEndpoint(SharesOf(nc), endpoint)->share_id == share->share_id);
}

void CutA(Trio& trio) {
  trio.network.Disconnect(kA, kB);
  trio.network.Disconnect(kA, kC);
  trio.network.Disconnect(kB, kA);
  trio.network.Disconnect(kC, kA);
  trio.network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  trio.network.SetAvailability(kA, kC, EndpointAvailability::Offline);
}

void RestoreA(Trio& trio) {
  trio.network.Reconnect(kA, kB);
  trio.network.Reconnect(kA, kC);
  trio.network.Reconnect(kB, kA);
  trio.network.Reconnect(kC, kA);
  trio.network.SetAvailability(kA, kB, EndpointAvailability::Online);
  trio.network.SetAvailability(kA, kC, EndpointAvailability::Online);
}

void TestThreeReplicasConverge() {
  Trio trio;
  auto node = MakeNode(trio.a);
  AddRecord(*node, "seed", kA, 1);
  auto const node_id = node.id();
  auto const to_b = trio.a.sync->OfferNode(
      node, MakeLink(*trio.a.domain, kB), ShareAccess::ReadWrite);
  auto world = trio.world();
  Pump(world, [&] { return Joined(trio.a, trio.b, to_b, node_id); }, 40);
  auto const to_c =
      trio.c.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  std::vector<std::uint8_t> saved_add;
  Pump(world, [&] { return ThreeWay(trio, node_id); }, 80, &saved_add);
  CHECK(!saved_add.empty());
  ExpectSameRecords(trio.a.sync->FindNode(node_id),
                    trio.b.sync->FindNode(node_id));
  ExpectSameRecords(trio.a.sync->FindNode(node_id),
                    trio.c.sync->FindNode(node_id));
  ExpectSameShares(trio.a.sync->FindNode(node_id),
                   trio.b.sync->FindNode(node_id));
  ExpectSameShares(trio.a.sync->FindNode(node_id),
                   trio.c.sync->FindNode(node_id));
  CHECK(!AsTopo(trio.b.sync->FindNode(node_id))->local_secret.is_valid());
  CHECK(!AsTopo(trio.c.sync->FindNode(node_id))->local_secret.is_valid());
  ExpectProtocolShareId(trio, node_id, kC);
  CHECK(Joined(trio.a, trio.c, to_c, node_id));

  auto const shares = SharesOf(trio.a.sync->FindNode(node_id));
  CHECK(FindEndpoint(shares, kA)->access == ShareAccess::ReadWrite);
  CHECK(FindEndpoint(shares, kB)->access == ShareAccess::ReadWrite);
  CHECK(FindEndpoint(shares, kC)->access == ShareAccess::ReadWrite);
  CHECK(FindEndpoint(shares, kB)->share_id !=
        FindEndpoint(shares, kC)->share_id);
}

void TestDirectExchangeWhileHolderIsDown() {
  Trio trio;
  auto node = MakeNode(trio.a);
  AddRecord(*node, "seed", kA, 1);
  auto const node_id = node.id();
  node = {};
  auto const to_b = trio.a.sync->OfferNode(
      AsTopo(trio.a.sync->FindNode(node_id)), MakeLink(*trio.a.domain, kB),
      ShareAccess::ReadWrite);
  auto world = trio.world();
  Pump(world, [&] { return Joined(trio.a, trio.b, to_b, node_id); }, 40);
  trio.c.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  Pump(world, [&] { return ThreeWay(trio, node_id); }, 80);

  CutA(trio);
  AddRecord(*AsTopo(trio.b.sync->FindNode(node_id)), "from-b", kB, 1);
  AddRecord(*AsTopo(trio.c.sync->FindNode(node_id)), "from-c", kC, 1);
  Pump(
      world,
      [&] {
        return HasText(trio.b.sync->FindNode(node_id), "from-c", kC, 1) &&
               HasText(trio.c.sync->FindNode(node_id), "from-b", kB, 1) &&
               !HasText(trio.a.sync->FindNode(node_id), "from-b", kB, 1);
      },
      80);
  CHECK(!HasText(trio.a.sync->FindNode(node_id), "from-c", kC, 1));

  RestoreA(trio);
  Pump(
      world,
      [&] {
        return HasText(trio.a.sync->FindNode(node_id), "from-b", kB, 1) &&
               HasText(trio.a.sync->FindNode(node_id), "from-c", kC, 1);
      },
      80);
  ExpectSameRecords(trio.a.sync->FindNode(node_id),
                    trio.b.sync->FindNode(node_id));
  ExpectSameRecords(trio.b.sync->FindNode(node_id),
                    trio.c.sync->FindNode(node_id));
  ExpectSameShares(trio.a.sync->FindNode(node_id),
                   trio.c.sync->FindNode(node_id));
}

void TestSimultaneousJoin() {
  Trio trio;
  auto node = MakeNode(trio.a);
  AddRecord(*node, "seed", kA, 1);
  auto const node_id = node.id();
  auto const to_b = trio.a.sync->OfferNode(
      node, MakeLink(*trio.a.domain, kB), ShareAccess::ReadWrite);
  auto const to_c = trio.a.sync->OfferNode(
      node, MakeLink(*trio.a.domain, kC), ShareAccess::ReadWrite);
  CHECK(to_b != to_c);
  auto world = trio.world();
  Pump(
      world,
      [&] {
        return Joined(trio.a, trio.b, to_b, node_id) &&
               Joined(trio.a, trio.c, to_c, node_id) &&
               ThreeWay(trio, node_id);
      },
      120);
  ExpectProtocolShareId(trio, node_id, kC);
  auto const* b_on_c = FindEndpoint(SharesOf(trio.c.sync->FindNode(node_id)), kB);
  CHECK(b_on_c != nullptr);
  CHECK(AddEventObjectId(trio.a.sync->FindNode(node_id), b_on_c->share_id) ==
        b_on_c->share_id);
}

void TestEventDuringJoin() {
  Trio trio;
  auto node = MakeNode(trio.a);
  AddRecord(*node, "seed", kA, 1);
  auto const node_id = node.id();
  auto const to_b = trio.a.sync->OfferNode(
      node, MakeLink(*trio.a.domain, kB), ShareAccess::ReadWrite);
  auto world = trio.world();
  Pump(world, [&] { return Joined(trio.a, trio.b, to_b, node_id); }, 40);
  auto const to_c =
      trio.c.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  CHECK(trio.network.DeliverNext(kC, kA));
  CHECK(trio.a.sync->OfferPhase(to_c) == ShareOfferPhase::Accepted);
  CHECK(trio.network.PendingCount(kA, kC) >= 1);
  AddRecord(*AsTopo(trio.a.sync->FindNode(node_id)), "during", kA, 2);
  AddRecord(*AsTopo(trio.b.sync->FindNode(node_id)), "while-joining", kB, 1);
  Pump(
      world,
      [&] {
        return ThreeWay(trio, node_id) &&
               HasText(trio.c.sync->FindNode(node_id), "during", kA, 2) &&
               HasText(trio.c.sync->FindNode(node_id), "while-joining", kB, 1);
      },
      80);
  ExpectSameRecords(trio.a.sync->FindNode(node_id),
                    trio.c.sync->FindNode(node_id));
}

void TestLostAckKeepsShareId() {
  Trio trio;
  auto node = MakeNode(trio.a);
  AddRecord(*node, "seed", kA, 1);
  auto const node_id = node.id();
  node = {};
  auto const to_b = trio.a.sync->OfferNode(
      AsTopo(trio.a.sync->FindNode(node_id)), MakeLink(*trio.a.domain, kB),
      ShareAccess::ReadWrite);
  auto world = trio.world();
  Pump(world, [&] { return Joined(trio.a, trio.b, to_b, node_id); }, 40);
  trio.c.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  std::vector<std::uint8_t> saved_add;
  Pump(
      world,
      [&] {
        return !saved_add.empty() &&
               trio.b.sync->FindNode(node_id).is_valid() &&
               trio.b.sync->FindNode(node_id)->shares.size() == 3;
      },
      80, &saved_add);
  CHECK(!saved_add.empty());
  EventFrame first;
  CHECK(DecodeEventFrame(saved_add, first));
  auto const share_before =
      FindEndpoint(SharesOf(trio.b.sync->FindNode(node_id)), kC)->share_id;

  // The acknowledgement of that AddShare may already have been delivered.
  // Force one more retry of a later business event and of a duplicate of
  // the original topology packet.
  Pump(world, [&] { return ThreeWay(trio, node_id); }, 80);
  auto const share_after =
      FindEndpoint(SharesOf(trio.a.sync->FindNode(node_id)), kC)->share_id;
  CHECK(share_before == share_after);
  CHECK(FindEndpoint(SharesOf(trio.c.sync->FindNode(node_id)), kC)->share_id ==
        share_after);

  AddRecord(*AsTopo(trio.b.sync->FindNode(node_id)), "retry-me", kB, 4);
  trio.network.ClearQueues();
  trio.b.sync->Service(1000 * kShareOfferRetryIntervalUs);
  auto const toward_c = FindEndpoint(SharesOf(trio.b.sync->FindNode(node_id)), kC);
  CHECK(toward_c != nullptr);
  auto const sync_index = trio.b.sync->FindNode(node_id)
                              ->FindLinkSyncIndexForShare(toward_c->share_id);
  auto state = trio.b.sync->FindNode(node_id)->link_sync_states[sync_index];
  if (!state.is_loaded()) {
    state.Load();
  }
  CHECK(state->HasPendingEvent());
  auto const frozen = state->pending_event_packet;
  CHECK(trio.network.PeekNext(kB, kC) == frozen);
  CHECK(trio.network.DeliverNext(kB, kC));
  CHECK(trio.network.DropNext(kC, kB));
  CHECK(state->pending_event_packet == frozen);
  trio.b.sync->Service(2000 * kShareOfferRetryIntervalUs);
  CHECK(state->pending_event_packet == frozen);
  CHECK(trio.network.PeekNext(kB, kC) == frozen);
  CHECK(trio.network.DeliverNext(kB, kC));
  CHECK(trio.network.DeliverNext(kC, kB));
  CHECK(HasText(trio.c.sync->FindNode(node_id), "retry-me", kB, 4));
  CHECK(Observe(*AsTopo(trio.c.sync->FindNode(node_id))).size() ==
        Observe(*AsTopo(trio.b.sync->FindNode(node_id))).size());
  CHECK(FindEndpoint(SharesOf(trio.b.sync->FindNode(node_id)), kC)->share_id ==
        share_after);

  trio.b.transport->Send(kC, saved_add);
  CHECK(trio.network.DeliverNext(kB, kC));
  CHECK(FindEndpoint(SharesOf(trio.c.sync->FindNode(node_id)), kC)->share_id ==
        share_after);
  CHECK(trio.c.sync->FindNode(node_id)->shares.size() == 3);
  (void)first;
}

void TestRestartKeepsShareIdentity() {
  Trio trio;
  auto node = MakeNode(trio.a);
  AddRecord(*node, "seed", kA, 1);
  auto const node_id = node.id();
  node = {};
  auto const to_b = trio.a.sync->OfferNode(
      AsTopo(trio.a.sync->FindNode(node_id)), MakeLink(*trio.a.domain, kB),
      ShareAccess::ReadWrite);
  auto world = trio.world();
  Pump(world, [&] { return Joined(trio.a, trio.b, to_b, node_id); }, 40);
  trio.c.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  Pump(world, [&] { return ThreeWay(trio, node_id); }, 80);
  auto const before = SharesOf(trio.b.sync->FindNode(node_id));
  auto const b_event = AddEventObjectId(trio.b.sync->FindNode(node_id),
                                        FindEndpoint(before, kC)->share_id);
  CHECK(b_event != FindEndpoint(before, kC)->share_id);

  trio.b.Stop();
  trio.network.ClearQueues();
  trio.b.Start();
  auto const after = SharesOf(trio.b.sync->FindNode(node_id));
  CHECK(after.size() == before.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    CHECK(after[i].share_id == before[i].share_id);
    CHECK(after[i].endpoint == before[i].endpoint);
    CHECK(after[i].access == before[i].access);
    CHECK(after[i].link_id == before[i].link_id);
  }
  CHECK(AddEventObjectId(trio.b.sync->FindNode(node_id),
                         FindEndpoint(after, kC)->share_id) == b_event);
  CHECK(AddEventObjectId(trio.b.sync->FindNode(node_id),
                         FindEndpoint(after, kC)->share_id) !=
        FindEndpoint(after, kC)->share_id);

  AddRecord(*AsTopo(trio.c.sync->FindNode(node_id)), "after-restart", kC, 3);
  Pump(
      world,
      [&] {
        return HasText(trio.b.sync->FindNode(node_id), "after-restart", kC, 3) &&
               HasText(trio.a.sync->FindNode(node_id), "after-restart", kC, 3);
      },
      80);
  ExpectSameShares(trio.a.sync->FindNode(node_id),
                   trio.b.sync->FindNode(node_id));
}

void TestAccessChangePropagates() {
  Trio trio;
  auto node = MakeNode(trio.a);
  AddRecord(*node, "seed", kA, 1);
  auto const node_id = node.id();
  node = {};
  auto const to_b = trio.a.sync->OfferNode(
      AsTopo(trio.a.sync->FindNode(node_id)), MakeLink(*trio.a.domain, kB),
      ShareAccess::ReadWrite);
  auto world = trio.world();
  Pump(world, [&] { return Joined(trio.a, trio.b, to_b, node_id); }, 40);
  trio.c.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  Pump(world, [&] { return ThreeWay(trio, node_id); }, 80);

  auto const b_share =
      FindEndpoint(SharesOf(trio.a.sync->FindNode(node_id)), kB)->share_id;
  trio.a.sync->ChangeShareAccess(node_id, b_share, ShareAccess::ReadOnly);
  Pump(
      world,
      [&] {
        auto on_b = FindEndpoint(SharesOf(trio.b.sync->FindNode(node_id)), kB);
        auto on_c = FindEndpoint(SharesOf(trio.c.sync->FindNode(node_id)), kB);
        return on_b != nullptr && on_c != nullptr &&
               on_b->access == ShareAccess::ReadOnly &&
               on_c->access == ShareAccess::ReadOnly &&
               on_b->share_id == b_share && on_c->share_id == b_share;
      },
      80);

  AddRecord(*AsTopo(trio.b.sync->FindNode(node_id)), "revoked-write", kB, 7);
  trio.network.ClearQueues();
  trio.b.sync->Service(1000 * kShareOfferRetryIntervalUs);
  trio.b.sync->Service(2000 * kShareOfferRetryIntervalUs);
  for (auto const* dest : {&kA, &kC}) {
    auto const& queued = trio.network.PeekNext(kB, *dest);
    if (queued.empty()) {
      continue;
    }
    EventFrame frame;
    CHECK(DecodeEventFrame(queued, frame));
    CHECK(frame.event_class_id != AddTopoEvent::kClassId);
    CHECK(frame.identity.origin_uid != kB ||
          frame.identity.origin_sequence != 7);
  }
  trio.network.ClearQueues();
  CHECK(!HasText(trio.a.sync->FindNode(node_id), "revoked-write", kB, 7));
  CHECK(!HasText(trio.c.sync->FindNode(node_id), "revoked-write", kB, 7));

  AddRecord(*AsTopo(trio.a.sync->FindNode(node_id)), "still-from-a", kA, 8);
  Pump(
      world,
      [&] {
        return HasText(trio.b.sync->FindNode(node_id), "still-from-a", kA, 8) &&
               HasText(trio.c.sync->FindNode(node_id), "still-from-a", kA, 8);
      },
      40);
}

void TestRemoveShareIsNotRestored() {
  Trio trio;
  auto node = MakeNode(trio.a);
  AddRecord(*node, "seed", kA, 1);
  auto const node_id = node.id();
  node = {};
  auto const to_b = trio.a.sync->OfferNode(
      AsTopo(trio.a.sync->FindNode(node_id)), MakeLink(*trio.a.domain, kB),
      ShareAccess::ReadWrite);
  auto world = trio.world();
  Pump(world, [&] { return Joined(trio.a, trio.b, to_b, node_id); }, 40);
  trio.c.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  std::vector<std::uint8_t> saved_add;
  Pump(world, [&] { return ThreeWay(trio, node_id); }, 80, &saved_add);
  CHECK(!saved_add.empty());
  auto const c_share =
      FindEndpoint(SharesOf(trio.a.sync->FindNode(node_id)), kC)->share_id;
  EventFrame saved;
  CHECK(DecodeEventFrame(saved_add, saved));
  CHECK(saved.event_class_id == AddShareEvent::kClassId);

  trio.a.sync->RemoveShare(node_id, c_share);
  Pump(
      world,
      [&] {
        return FindEndpoint(SharesOf(trio.a.sync->FindNode(node_id)), kC) ==
                   nullptr &&
               FindEndpoint(SharesOf(trio.b.sync->FindNode(node_id)), kC) ==
                   nullptr &&
               FindEndpoint(SharesOf(trio.c.sync->FindNode(node_id)), kC) ==
                   nullptr;
      },
      80);
  CHECK(trio.a.sync->FindNode(node_id)->shares.size() == 2);
  CHECK(trio.b.sync->FindNode(node_id)->shares.size() == 2);
  CHECK(trio.c.sync->FindNode(node_id)->shares.size() == 2);

  trio.a.transport->Send(kB, saved_add);
  CHECK(trio.network.DeliverNext(kA, kB));
  trio.a.transport->Send(kB, saved_add);
  CHECK(trio.network.DeliverNext(kA, kB));
  CHECK(FindEndpoint(SharesOf(trio.b.sync->FindNode(node_id)), kC) == nullptr);
  CHECK(trio.b.sync->FindNode(node_id)->shares.size() == 2);
  CHECK(FindEndpoint(SharesOf(trio.a.sync->FindNode(node_id)), kC) == nullptr);

  AddRecord(*AsTopo(trio.c.sync->FindNode(node_id)), "after-remove", kC, 1);
  std::uint64_t now = 1000 * kShareOfferRetryIntervalUs;
  trio.c.sync->Service(0);
  trio.c.sync->Service(now);
  CHECK(!HasText(trio.a.sync->FindNode(node_id), "after-remove", kC, 1));
  CHECK(!HasText(trio.b.sync->FindNode(node_id), "after-remove", kC, 1));
}

void TestDeterministicThreeReplicaChaos() {
  constexpr std::uint32_t kSeed = 0x3c1e0919u;
  constexpr int kSteps = 3000;
  std::mt19937 rng{kSeed};

  Trio trio;
  auto node = MakeNode(trio.a);
  AddRecord(*node, "seed", kA, 1);
  auto const node_id = node.id();
  node = {};
  auto const to_b = trio.a.sync->OfferNode(
      AsTopo(trio.a.sync->FindNode(node_id)), MakeLink(*trio.a.domain, kB),
      ShareAccess::ReadWrite);
  auto world = trio.world();
  Pump(world, [&] { return Joined(trio.a, trio.b, to_b, node_id); }, 40);
  trio.c.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  Pump(world, [&] { return ThreeWay(trio, node_id); }, 80);

  Replica* reps[3] = {&trio.a, &trio.b, &trio.c};
  std::string const ids[3] = {kA, kB, kC};
  std::uint64_t seq[3] = {2, 1, 1};
  int written = 0;
  auto service_all = [&](std::uint64_t now) {
    for (auto* replica : reps) {
      replica->sync->Service(now);
    }
  };
  auto queued = [&] {
    for (int from = 0; from < 3; ++from) {
      for (int to = 0; to < 3; ++to) {
        if (from != to &&
            trio.network.PendingCount(ids[from], ids[to]) != 0) {
          return true;
        }
      }
    }
    return false;
  };

  for (int step = 0; step < kSteps; ++step) {
    auto const roll = rng() % 100;
    auto const i = static_cast<int>(rng() % 3);
    auto const j = static_cast<int>(rng() % 3);
    // Network faults on every step. Events and restarts are on a fixed
    // stride: each commit replays the journal, so an event on most steps
    // would make the scenario unbounded.
    if (i != j) {
      switch (roll % 5) {
        case 0:
          trio.network.DeliverNext(ids[i], ids[j]);
          break;
        case 1:
          trio.network.DropNext(ids[i], ids[j]);
          break;
        case 2:
          trio.network.DuplicateNext(ids[i], ids[j]);
          break;
        case 3:
          trio.network.DeferNext(ids[i], ids[j]);
          break;
        default:
          trio.network.SetAvailability(
              ids[i], ids[j],
              (roll % 2) == 0 ? EndpointAvailability::Offline
                              : EndpointAvailability::Online);
          break;
      }
    }
    if ((step % 40) == 0) {
      auto live = reps[i]->sync->FindNode(node_id);
      if (live.is_valid()) {
        AddRecord(*AsTopo(live), "c" + std::to_string(written), ids[i],
                  seq[i]++);
        ++written;
      }
    }
    if ((step % 400) == 399) {
      reps[i]->Stop();
      reps[i]->Start();
    }
    if ((step % 80) == 0) {
      service_all(static_cast<std::uint64_t>(step) *
                  kShareOfferRetryIntervalUs);
    }
  }

  for (int from = 0; from < 3; ++from) {
    for (int to = 0; to < 3; ++to) {
      if (from == to) {
        continue;
      }
      trio.network.Reconnect(ids[from], ids[to]);
      trio.network.SetAvailability(ids[from], ids[to],
                                   EndpointAvailability::Online);
    }
  }

  std::uint64_t now = static_cast<std::uint64_t>(kSteps) *
                      kShareOfferRetryIntervalUs;
  auto drain = [&] {
    for (int n = 0; n < 50000; ++n) {
      if (!DeliverRound(world, nullptr)) {
        return;
      }
    }
  };
  bool settled = false;
  for (int step = 0; step < 600; ++step) {
    drain();
    now += kShareOfferRetryIntervalUs;
    auto const sends = trio.a.sends + trio.b.sends + trio.c.sends;
    service_all(now);
    if (!queued() &&
        trio.a.sends + trio.b.sends + trio.c.sends == sends &&
        ThreeWay(trio, node_id)) {
      settled = true;
      break;
    }
  }
  CHECK(settled);
  CHECK(ThreeWay(trio, node_id));
  CHECK(written > 0);

  auto const sends = trio.a.sends + trio.b.sends + trio.c.sends;
  for (int step = 0; step < 40; ++step) {
    now += kShareOfferRetryIntervalUs;
    service_all(now);
  }
  CHECK(trio.a.sends + trio.b.sends + trio.c.sends == sends);
  CHECK(!queued());
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestThreeReplicasConverge();
  apptraverse::test::TestDirectExchangeWhileHolderIsDown();
  apptraverse::test::TestSimultaneousJoin();
  apptraverse::test::TestEventDuringJoin();
  apptraverse::test::TestLostAckKeepsShareId();
  apptraverse::test::TestRestartKeepsShareIdentity();
  apptraverse::test::TestAccessChangePropagates();
  apptraverse::test::TestRemoveShareIsNotRestored();
  apptraverse::test::TestDeterministicThreeReplicaChaos();
  std::cout << "shared_node_topology_test OK\n";
  return 0;
}
